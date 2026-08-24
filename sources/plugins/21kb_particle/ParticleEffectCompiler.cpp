#include "ParticleEffectCompiler.hpp"

#include "engine/assets/AssetKind.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace kb::particle_plugin {
namespace {

void Add(std::vector<kb::scene::ParticleEffectDiagnostic>& diagnostics,
         kb::scene::ParticleEffectDiagnosticCode code, std::string path, std::string message,
         kb::scene::ParticleStableId emitterId = 0U, kb::scene::ParticleStableId moduleId = 0U) {
    diagnostics.push_back({.code = code, .propertyPath = std::move(path), .emitterId = emitterId,
                           .moduleId = moduleId, .message = std::move(message)});
}

[[nodiscard]] std::uint64_t Resolve(const kb::scene::ParticleAssetReference& reference,
                                    const kb::assets::AssetRegistry& registry) noexcept {
    if (reference.assetId != 0U) return reference.assetId;
    if (reference.virtualPath.empty()) return 0U;
    const kb::assets::AssetMetadata* metadata = registry.FindByPath(reference.virtualPath);
    return metadata != nullptr ? metadata->id.value : 0U;
}

[[nodiscard]] bool Supports(kb::scene::ParticleOutputType type, const ParticleCompilerCapabilities& capabilities) {
    switch (type) {
    case kb::scene::ParticleOutputType::Billboard: return capabilities.billboard;
    case kb::scene::ParticleOutputType::StretchedBillboard: return capabilities.stretchedBillboard;
    case kb::scene::ParticleOutputType::PointSprite: return capabilities.pointSprite;
    case kb::scene::ParticleOutputType::Mesh: return capabilities.mesh;
    case kb::scene::ParticleOutputType::Trail: return capabilities.trail;
    case kb::scene::ParticleOutputType::Ribbon: return capabilities.ribbon;
    case kb::scene::ParticleOutputType::Beam: return capabilities.beam;
    case kb::scene::ParticleOutputType::Volumetric: return capabilities.volumetric;
    }
    return false;
}

} // namespace

std::uint64_t ParticleCompilerCapabilities::StableKey() const noexcept {
    return (billboard ? 1ULL : 0ULL) | (stretchedBillboard ? 2ULL : 0ULL) | (pointSprite ? 4ULL : 0ULL) |
        (mesh ? 8ULL : 0ULL) | (trail ? 16ULL : 0ULL) | (ribbon ? 32ULL : 0ULL) |
        (beam ? 64ULL : 0ULL) | (volumetric ? 128ULL : 0ULL);
}

std::vector<kb::scene::ParticleEffectDiagnostic> ParticleEffectCompiler::ValidateCapabilities(
    const kb::scene::ParticleEffectAsset& asset, const ParticleCompileRequest& request) {
    std::vector<kb::scene::ParticleEffectDiagnostic> diagnostics;
    diagnostics.reserve(1U + asset.emitters.size() + asset.eventBindings.size());
    for (std::size_t bindingIndex = 0U; bindingIndex < asset.eventBindings.size(); ++bindingIndex) {
        const auto& binding = asset.eventBindings[bindingIndex];
        if (binding.action == kb::scene::ParticleEventAction::EmitEffectAsset) {
            Add(diagnostics, kb::scene::ParticleEffectDiagnosticCode::UnsupportedCapability,
                "effect.eventBinding[" + std::to_string(bindingIndex) + "].action",
                "external particle effect events are not executable by the current compiler", binding.sourceEmitterId,
                binding.sourceModuleId);
        }
        if (binding.sourceModuleId == 0U) continue;
        const auto emitter = std::find_if(asset.emitters.begin(), asset.emitters.end(), [&](const auto& candidate) {
            return candidate.emitterId == binding.sourceEmitterId;
        });
        if (emitter == asset.emitters.end()) continue;
        const auto module = std::find_if(emitter->modules.begin(), emitter->modules.end(), [&](const auto& candidate) {
            return candidate.moduleId == binding.sourceModuleId;
        });
        if (module == emitter->modules.end()) continue;
        if (module->type != kb::scene::ParticleModuleType::CollisionPlane ||
            binding.trigger != kb::scene::ParticleEventTrigger::Collision) {
            Add(diagnostics, kb::scene::ParticleEffectDiagnosticCode::UnsupportedCapability,
                "effect.eventBinding[" + std::to_string(bindingIndex) + "].sourceModuleId",
                "the selected source module cannot emit runtime events", binding.sourceEmitterId,
                binding.sourceModuleId);
        }
    }
    for (std::size_t emitterIndex = 0U; emitterIndex < asset.emitters.size(); ++emitterIndex) {
        const auto& emitter = asset.emitters[emitterIndex];
        if (!Supports(emitter.output.type, request.capabilities)) {
            Add(diagnostics, kb::scene::ParticleEffectDiagnosticCode::UnsupportedCapability,
                "effect.emitter[" + std::to_string(emitterIndex) + "].output.type",
                "particle output is not executable by the current compiler capability set", emitter.emitterId);
        }
    }
    return diagnostics;
}

ParticleCompileResult ParticleEffectCompiler::Compile(const kb::scene::ParticleEffectAsset& asset,
                                                      const kb::assets::AssetMetadata& owner,
                                                      const kb::assets::AssetRegistry& registry,
                                                      const ParticleCompileRequest& request) {
    ParticleCompileResult result;
    const kb::scene::ParticleEffectValidationResult structure =
        kb::scene::ParticleEffectAssetValidator::ValidateStructure(asset);
    result.diagnostics = structure.diagnostics;
    if (!structure.Succeeded()) return result;

    const std::vector<kb::scene::ParticleEffectDiagnostic> capabilities = ValidateCapabilities(asset, request);
    result.diagnostics.insert(result.diagnostics.end(), capabilities.begin(), capabilities.end());
    if (kb::scene::ParticleEffectDiagnosticsHaveErrors(capabilities)) return result;

    const kb::scene::ParticleEffectDependencyResult dependencies =
        kb::scene::ParticleEffectAssetValidator::ValidateDependencies(asset, owner, registry);
    result.transitiveDependencies = dependencies.transitiveDependencies;
    result.diagnostics.insert(result.diagnostics.end(), dependencies.diagnostics.begin(),
        dependencies.diagnostics.end());
    if (!dependencies.Succeeded()) return result;

    kb::particles::ParticleCompiledEffect compiled{};
    compiled.determinismSeed = asset.determinismSeed;
    compiled.durationSeconds = asset.durationSeconds;
    compiled.looping = asset.looping;
    compiled.backendPolicy = asset.backendPolicy;
    compiled.gpuCatchupPolicy = asset.gpuCatchupPolicy;
    compiled.emitterCount = static_cast<std::uint8_t>(asset.emitters.size());
    std::array<const kb::scene::ParticleEmitterAsset*, kb::scene::kParticleEffectMaxEmitters>
        emittersByAuthoringOrder{};
    for (const kb::scene::ParticleEmitterAsset& emitter : asset.emitters)
        emittersByAuthoringOrder[emitter.authoringOrder] = &emitter;
    for (std::size_t emitterIndex = 0U; emitterIndex < asset.emitters.size(); ++emitterIndex) {
        const kb::scene::ParticleEmitterAsset& source = *emittersByAuthoringOrder[emitterIndex];
        auto& destination = compiled.emitters[emitterIndex];
        destination.emitterId = source.emitterId;
        destination.outputType = source.output.type;
        destination.materialAssetId = Resolve(source.output.material, registry);
        destination.meshAssetId = Resolve(source.output.mesh, registry);
        destination.textureAtlasAssetId = Resolve(source.output.textureAtlas, registry);
        destination.blendMode = source.output.blend;
        destination.sortMode = source.output.sort;
        destination.alignment = source.output.alignment;
        destination.depthTest = source.output.depthTest;
        destination.depthWrite = source.output.depthWrite;
        destination.softParticles = source.output.softParticles;
        destination.antiAliasing = source.output.antiAliasing;
        const auto compileFlipbook = [&](const kb::scene::ParticleFlipbookAsset& flipbook) noexcept {
            destination.flipbookColumns = static_cast<std::uint16_t>(flipbook.columns);
            destination.flipbookRows = static_cast<std::uint16_t>(flipbook.rows);
            destination.flipbookFrameCount = flipbook.columns * flipbook.rows;
            destination.flipbookFramesPerSecond = flipbook.framesPerSecond;
            destination.flipbookLooping = flipbook.looping;
        };
        if (source.output.type == kb::scene::ParticleOutputType::Billboard) {
            compileFlipbook(std::get<kb::scene::ParticleBillboardOutput>(source.output.payload).flipbook);
        } else if (source.output.type == kb::scene::ParticleOutputType::StretchedBillboard) {
            const auto& value = std::get<kb::scene::ParticleStretchedBillboardOutput>(source.output.payload);
            compileFlipbook(value.flipbook);
            destination.stretchVelocityScale = value.velocityScale;
            destination.stretchMinimumLength = value.minimumLength;
        } else if (source.output.type == kb::scene::ParticleOutputType::PointSprite) {
            const auto& value = std::get<kb::scene::ParticlePointSpriteOutput>(source.output.payload);
            compileFlipbook(value.flipbook);
            destination.pointSpriteDiameter = value.diameter;
        } else if (source.output.type == kb::scene::ParticleOutputType::Mesh) {
            const auto& value = std::get<kb::scene::ParticleMeshOutput>(source.output.payload);
            destination.meshLodBias = value.lodBias;
            destination.meshCastsShadow = value.castsShadow;
            destination.meshReceivesShadow = value.receivesShadow;
        } else if (source.output.type == kb::scene::ParticleOutputType::Trail) {
            const auto& value = std::get<kb::scene::ParticleTrailOutput>(source.output.payload);
            destination.trailSampleIntervalSeconds = value.sampleIntervalSeconds;
            destination.trailMinimumDistance = value.minimumDistance;
            destination.trailMaxSamplesPerParticle = value.maxSamplesPerParticle;
            destination.trailWidth = value.width;
        } else if (source.output.type == kb::scene::ParticleOutputType::Ribbon) {
            const auto& value = std::get<kb::scene::ParticleRibbonOutput>(source.output.payload);
            destination.ribbonMaxSegments = value.maxSegments;
            destination.ribbonWidth = value.width;
            destination.ribbonBreakOnDeath = value.breakOnDeath;
        } else if (source.output.type == kb::scene::ParticleOutputType::Beam) {
            const auto& value = std::get<kb::scene::ParticleBeamOutput>(source.output.payload);
            destination.beamLocalEnd = value.localEnd;
            destination.beamSegments = value.segments;
            destination.beamWidth = value.width;
            destination.beamNoiseAmplitude = value.noiseAmplitude;
            destination.beamNoiseFrequency = value.noiseFrequency;
        } else if (source.output.type == kb::scene::ParticleOutputType::Volumetric) {
            const auto& value = std::get<kb::scene::ParticleVolumetricOutput>(source.output.payload);
            destination.volumetricDensity = value.density;
            destination.volumetricRadiusScale = value.radiusScale;
            destination.volumetricLowQualitySteps = value.lowQualitySteps;
            destination.volumetricHighQualitySteps = value.highQualitySteps;
        }
        destination.simulationSpace = source.simulationSpace;
        destination.enabled = source.enabled;
        destination.mode = source.spawn.mode;
        destination.maxParticles = source.maxParticles;
        destination.localPosition = source.localPosition;
        destination.localRotation = source.localRotation;
        destination.initialVelocity = {.direction = kb::math::Normalize(source.spawn.direction),
                                       .speedMin = source.spawn.speedMin, .speedMax = source.spawn.speedMax,
                                       .randomization = source.spawn.randomization,
                                       .spreadDegrees = source.spawn.spreadDegrees};
        destination.lifetimeMin = source.spawn.lifetimeMin;
        destination.lifetimeMax = source.spawn.lifetimeMax;
        destination.prewarmSeconds = source.spawn.prewarmSeconds;
        destination.rateKeyCount = static_cast<std::uint8_t>(source.spawn.rateOverTime.keyframes.size());
        destination.burstCount = static_cast<std::uint8_t>(source.spawn.bursts.size());
        for (std::size_t index = 0U; index < source.spawn.rateOverTime.keyframes.size(); ++index) {
            const auto& key = source.spawn.rateOverTime.keyframes[index];
            destination.rateKeys[index] = {.time = key.time, .value = key.value, .easing = key.easing};
        }
        std::copy(source.spawn.bursts.begin(), source.spawn.bursts.end(), destination.bursts.begin());
        destination.moduleCount = static_cast<std::uint8_t>(source.modules.size());
        std::array<const kb::scene::ParticleModuleAsset*, kb::scene::kParticleEffectMaxModulesPerEmitter>
            modulesByAuthoringOrder{};
        for (const kb::scene::ParticleModuleAsset& module : source.modules)
            modulesByAuthoringOrder[module.authoringOrder] = &module;
        bool colorOverLifeEnabled = false;
        bool sizeOverLifeEnabled = false;
        for (std::size_t moduleIndex = 0U; moduleIndex < source.modules.size(); ++moduleIndex) {
            const auto& sourceModule = *modulesByAuthoringOrder[moduleIndex];
            auto& destinationModule = destination.modules[moduleIndex];
            destinationModule.moduleId = sourceModule.moduleId;
            destinationModule.type = sourceModule.type;
            destinationModule.enabled = sourceModule.enabled;
            switch (sourceModule.type) {
            case kb::scene::ParticleModuleType::InitialVelocity:
                destinationModule.payload = std::get<kb::scene::ParticleInitialVelocityModule>(sourceModule.payload);
                if (sourceModule.enabled) {
                    destination.initialVelocity = std::get<kb::scene::ParticleInitialVelocityModule>(sourceModule.payload);
                    destination.initialVelocity.direction = kb::math::Normalize(destination.initialVelocity.direction);
                }
                break;
            case kb::scene::ParticleModuleType::Gravity:
                destinationModule.payload = std::get<kb::scene::ParticleGravityModule>(sourceModule.payload); break;
            case kb::scene::ParticleModuleType::Wind:
                destinationModule.payload = std::get<kb::scene::ParticleWindModule>(sourceModule.payload); break;
            case kb::scene::ParticleModuleType::Drag:
                destinationModule.payload = std::get<kb::scene::ParticleDragModule>(sourceModule.payload); break;
            case kb::scene::ParticleModuleType::ColorOverLife: {
                if (!sourceModule.enabled) break;
                const auto& gradient = std::get<kb::scene::ParticleColorOverLifeModule>(sourceModule.payload).gradient;
                destination.colorOverLife.stopCount = static_cast<std::uint8_t>(gradient.stops.size());
                for (std::size_t index = 0U; index < gradient.stops.size(); ++index)
                    destination.colorOverLife.stops[index] = {.time = gradient.stops[index].time,
                                                               .color = gradient.stops[index].color};
                colorOverLifeEnabled = true;
                break;
            }
            case kb::scene::ParticleModuleType::SizeOverLife: {
                if (!sourceModule.enabled) break;
                const kb::math::Curve& curve = std::get<kb::scene::ParticleSizeOverLifeModule>(sourceModule.payload).curve;
                destination.sizeOverLife.keyCount = static_cast<std::uint8_t>(curve.keyframes.size());
                for (std::size_t index = 0U; index < curve.keyframes.size(); ++index)
                    destination.sizeOverLife.keys[index] = {.time = curve.keyframes[index].time,
                        .value = curve.keyframes[index].value, .easing = curve.keyframes[index].easing};
                sizeOverLifeEnabled = true;
                break;
            }
            case kb::scene::ParticleModuleType::AlphaOverLife: {
                if (!sourceModule.enabled) break;
                const kb::math::Curve& curve = std::get<kb::scene::ParticleAlphaOverLifeModule>(sourceModule.payload).curve;
                destination.alphaOverLife.keyCount = static_cast<std::uint8_t>(curve.keyframes.size());
                for (std::size_t index = 0U; index < curve.keyframes.size(); ++index)
                    destination.alphaOverLife.keys[index] = {.time = curve.keyframes[index].time,
                        .value = curve.keyframes[index].value, .easing = curve.keyframes[index].easing};
                break;
            }
            case kb::scene::ParticleModuleType::CollisionPlane: {
                auto collision = std::get<kb::scene::ParticleCollisionPlaneModule>(sourceModule.payload);
                const float length = std::sqrt(kb::math::Dot(collision.normal, collision.normal));
                collision.normal = collision.normal * (1.0F / length);
                collision.distance /= length;
                destinationModule.payload = collision;
                break;
            }
            case kb::scene::ParticleModuleType::SubEmitter:
                destinationModule.payload = std::get<kb::scene::ParticleSubEmitterModule>(sourceModule.payload); break;
            }
        }
        const kb::math::Color startColor = source.spawn.startColor;
        if (!colorOverLifeEnabled) {
            destination.colorOverLife.stopCount = 2U;
            destination.colorOverLife.stops[0] = {.time = 0.0F, .color = startColor};
            destination.colorOverLife.stops[1] = {.time = 1.0F, .color = startColor};
        } else {
            for (std::uint8_t index = 0U; index < destination.colorOverLife.stopCount; ++index) {
                kb::math::Color& color = destination.colorOverLife.stops[index].color;
                color.r *= startColor.r;
                color.g *= startColor.g;
                color.b *= startColor.b;
                color.a *= startColor.a;
            }
        }
        if (!sizeOverLifeEnabled) {
            destination.sizeOverLife.keyCount = 1U;
            destination.sizeOverLife.keys[0] = {.time = 0.0F, .value = source.spawn.startSize,
                                                .easing = kb::math::Easing::Linear};
        } else {
            for (std::uint8_t index = 0U; index < destination.sizeOverLife.keyCount; ++index)
                destination.sizeOverLife.keys[index].value *= source.spawn.startSize;
        }
    }
    compiled.eventBindingCount = static_cast<std::uint8_t>(asset.eventBindings.size());
    for (std::size_t index = 0U; index < asset.eventBindings.size(); ++index) {
        const auto& source = asset.eventBindings[index];
        const auto sourceEmitter = std::find_if(compiled.emitters.begin(), compiled.emitters.begin() + compiled.emitterCount,
            [&](const auto& emitter) { return emitter.emitterId == source.sourceEmitterId; });
        const auto targetEmitter = std::find_if(compiled.emitters.begin(), compiled.emitters.begin() + compiled.emitterCount,
            [&](const auto& emitter) { return emitter.emitterId == source.targetEmitterId; });
        compiled.eventBindings[index] = {.sourceEmitterIndex = static_cast<std::uint8_t>(sourceEmitter - compiled.emitters.begin()),
            .trigger = source.trigger, .sourceModuleId = source.sourceModuleId,
            .targetEmitterIndex = static_cast<std::uint8_t>(targetEmitter - compiled.emitters.begin()),
            .count = source.count, .maxDepth = static_cast<std::uint8_t>(source.maxDepth),
            .perStepBudget = source.perStepBudget};
    }
    result.effect = kb::particles::MakeParticleCompiledEffect(std::move(compiled));
    return result;
}

} // namespace kb::particle_plugin
