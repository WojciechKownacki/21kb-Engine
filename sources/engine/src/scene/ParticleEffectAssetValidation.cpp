#include "engine/scene/ParticleEffectAssetValidation.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <sstream>
#include <type_traits>
#include <unordered_map>

namespace kb::scene {
namespace {

void Add(ParticleEffectValidationResult& result, ParticleEffectDiagnosticCode code, std::string path,
         std::string message, ParticleStableId emitterId = 0U, ParticleStableId moduleId = 0U) {
    result.diagnostics.push_back(ParticleEffectDiagnostic{
        .code = code,
        .severity = ParticleEffectDiagnosticSeverity::Error,
        .line = 0U,
        .propertyPath = std::move(path),
        .emitterId = emitterId,
        .moduleId = moduleId,
        .message = std::move(message),
    });
}

[[nodiscard]] bool Finite(float value) noexcept {
    return std::isfinite(value);
}
[[nodiscard]] bool Finite(kb::math::Vec3 value) noexcept {
    return Finite(value.x) && Finite(value.y) && Finite(value.z);
}
[[nodiscard]] bool Finite(kb::math::Quat value) noexcept {
    return Finite(value.x) && Finite(value.y) && Finite(value.z) && Finite(value.w);
}
[[nodiscard]] float LengthSquared(kb::math::Vec3 value) noexcept {
    return value.x * value.x + value.y * value.y + value.z * value.z;
}
[[nodiscard]] float LengthSquared(kb::math::Quat value) noexcept {
    return value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
}

template <typename Enum> [[nodiscard]] bool EnumInRange(Enum value, Enum last) noexcept {
    using Value = std::underlying_type_t<Enum>;
    return static_cast<Value>(value) <= static_cast<Value>(last);
}

void ValidateCurve(const kb::math::Curve& curve, std::string path, ParticleEffectValidationResult& result,
                   ParticleStableId emitterId, ParticleStableId moduleId, bool requireNonNegativeValue = false) {
    if (curve.keyframes.empty() || curve.keyframes.size() > kParticleEffectMaxCurveKeys) {
        Add(result, ParticleEffectDiagnosticCode::InvalidCurve, path, "curve key count is outside the supported range",
            emitterId, moduleId);
        return;
    }
    float previous = -1.0F;
    for (std::size_t index = 0U; index < curve.keyframes.size(); ++index) {
        const kb::math::CurveKeyframe& key = curve.keyframes[index];
        if (!Finite(key.time) || !Finite(key.value) || key.time < 0.0F || key.time > 1.0F || key.time <= previous ||
            (requireNonNegativeValue && key.value < 0.0F) || !EnumInRange(key.easing, kb::math::Easing::InOutBounce)) {
            Add(result, ParticleEffectDiagnosticCode::InvalidCurve, path + ".key[" + std::to_string(index) + "]",
                "curve keys must be finite, strictly ordered, and within their range", emitterId, moduleId);
        }
        previous = key.time;
    }
}

void ValidateGradient(const kb::math::Gradient& gradient, std::string path, ParticleEffectValidationResult& result,
                      ParticleStableId emitterId, ParticleStableId moduleId) {
    if (gradient.stops.empty() || gradient.stops.size() > kParticleEffectMaxGradientStops) {
        Add(result, ParticleEffectDiagnosticCode::InvalidGradient, path,
            "gradient stop count is outside the supported range", emitterId, moduleId);
        return;
    }
    float previous = -1.0F;
    for (std::size_t index = 0U; index < gradient.stops.size(); ++index) {
        const kb::math::GradientStop& stop = gradient.stops[index];
        const bool colorValid = Finite(stop.color.r) && Finite(stop.color.g) && Finite(stop.color.b) &&
                                Finite(stop.color.a) && stop.color.r >= 0.0F && stop.color.r <= 1.0F &&
                                stop.color.g >= 0.0F && stop.color.g <= 1.0F && stop.color.b >= 0.0F &&
                                stop.color.b <= 1.0F && stop.color.a >= 0.0F && stop.color.a <= 1.0F;
        if (!Finite(stop.time) || stop.time < 0.0F || stop.time > 1.0F || stop.time <= previous || !colorValid) {
            Add(result, ParticleEffectDiagnosticCode::InvalidGradient, path + ".stop[" + std::to_string(index) + "]",
                "gradient stops must be finite, strictly ordered, and normalized", emitterId, moduleId);
        }
        previous = stop.time;
    }
}

[[nodiscard]] bool PayloadMatchesType(const ParticleModuleAsset& module) noexcept {
    return static_cast<std::size_t>(module.type) == module.payload.index();
}

[[nodiscard]] bool OutputPayloadMatchesType(const ParticleOutputAsset& output) noexcept {
    return static_cast<std::size_t>(output.type) == output.payload.index();
}

void ValidateReference(const ParticleAssetReference& reference, std::string path,
                       ParticleEffectValidationResult& result, ParticleStableId emitterId = 0U,
                       ParticleStableId moduleId = 0U) {
    if (reference.virtualPath.size() > kParticleEffectMaxStringBytes) {
        Add(result, ParticleEffectDiagnosticCode::LimitExceeded, std::move(path), "asset reference path is too long",
            emitterId, moduleId);
    } else if (!IsValidParticleEffectString(reference.virtualPath)) {
        Add(result, ParticleEffectDiagnosticCode::InvalidUtf8, std::move(path), "asset reference path is not UTF-8",
            emitterId, moduleId);
    }
}

void ValidateFlipbook(const ParticleFlipbookAsset& flipbook, const ParticleAssetReference& atlas, std::string path,
                      ParticleEffectValidationResult& result, ParticleStableId emitterId) {
    constexpr std::uint32_t kMaxGridAxis = 256U;
    const std::uint64_t frameCount = static_cast<std::uint64_t>(flipbook.columns) * flipbook.rows;
    if (flipbook.columns == 0U || flipbook.rows == 0U || flipbook.columns > kMaxGridAxis ||
        flipbook.rows > kMaxGridAxis || frameCount > 65'536U || !Finite(flipbook.framesPerSecond) ||
        flipbook.framesPerSecond < 0.0F) {
        Add(result, ParticleEffectDiagnosticCode::InvalidValue, path,
            "flipbook grid and frame rate are outside the supported range", emitterId);
    }
    if ((frameCount > 1U || flipbook.framesPerSecond > 0.0F) && atlas.Empty()) {
        Add(result, ParticleEffectDiagnosticCode::InvalidReference, path + ".textureAtlas",
            "animated flipbook requires a texture atlas", emitterId);
    }
}

} // namespace

bool ParticleEffectValidationResult::Succeeded() const noexcept {
    return std::none_of(diagnostics.begin(), diagnostics.end(), [](const ParticleEffectDiagnostic& diagnostic) {
        return diagnostic.severity == ParticleEffectDiagnosticSeverity::Error;
    });
}

ParticleEffectValidationResult ParticleEffectAssetValidator::ValidateStructure(const ParticleEffectAsset& asset) {
    ParticleEffectValidationResult result{};
    if (asset.formatVersion != kParticleEffectFormatVersion)
        Add(result, ParticleEffectDiagnosticCode::UnsupportedVersion, "effect.formatVersion",
            "asset format version is unsupported");
    if (asset.effectId == 0U)
        Add(result, ParticleEffectDiagnosticCode::InvalidStableId, "effect.id", "effect id must be non-zero");
    if (asset.displayName.empty() || asset.displayName.size() > kParticleEffectMaxStringBytes)
        Add(result, ParticleEffectDiagnosticCode::InvalidValue, "effect.displayName",
            "display name must be non-empty and bounded");
    else if (!IsValidParticleEffectString(asset.displayName))
        Add(result, ParticleEffectDiagnosticCode::InvalidUtf8, "effect.displayName", "display name is not UTF-8");
    if (asset.recipeCategory.size() > kParticleEffectMaxStringBytes)
        Add(result, ParticleEffectDiagnosticCode::LimitExceeded, "effect.recipeCategory",
            "recipe category is too long");
    else if (!IsValidParticleEffectString(asset.recipeCategory))
        Add(result, ParticleEffectDiagnosticCode::InvalidUtf8, "effect.recipeCategory", "recipe category is not UTF-8");
    if (!EnumInRange(asset.backendPolicy, ParticleBackendPolicy::GpuVisualRequired))
        Add(result, ParticleEffectDiagnosticCode::InvalidEnum, "effect.backendPolicy", "backend policy is invalid");
    if (!EnumInRange(asset.gpuCatchupPolicy, ParticleGpuCatchupPolicy::BoundedWarmup))
        Add(result, ParticleEffectDiagnosticCode::InvalidEnum, "effect.gpuCatchupPolicy",
            "GPU catch-up policy is invalid");
    if (!Finite(asset.durationSeconds) || asset.durationSeconds < 0.0F)
        Add(result, ParticleEffectDiagnosticCode::InvalidValue, "effect.durationSeconds",
            "duration must be finite and non-negative");
    if (asset.emitters.empty() || asset.emitters.size() > kParticleEffectMaxEmitters)
        Add(result, ParticleEffectDiagnosticCode::LimitExceeded, "effect.emitterCount",
            "emitter count is outside the supported range");
    if (asset.eventBindings.size() > kParticleEffectMaxEventBindings)
        Add(result, ParticleEffectDiagnosticCode::LimitExceeded, "effect.eventBindingCount",
            "event binding count exceeds the hard limit");

    std::set<ParticleStableId> emitterIds;
    ParticleStableId previousEmitterId = 0U;
    std::uint64_t totalParticleCapacity = 0U;
    for (std::size_t emitterIndex = 0U; emitterIndex < asset.emitters.size(); ++emitterIndex) {
        const ParticleEmitterAsset& emitter = asset.emitters[emitterIndex];
        totalParticleCapacity += emitter.maxParticles;
        const std::string base = "effect.emitter[" + std::to_string(emitterIndex) + "]";
        if (emitter.emitterId == 0U || !emitterIds.insert(emitter.emitterId).second)
            Add(result, ParticleEffectDiagnosticCode::InvalidStableId, base + ".id",
                "emitter id must be unique and non-zero", emitter.emitterId);
        if (emitterIndex > 0U && emitter.emitterId <= previousEmitterId)
            Add(result, ParticleEffectDiagnosticCode::UnsortedStableId, base + ".id",
                "emitter ids must be strictly increasing", emitter.emitterId);
        previousEmitterId = emitter.emitterId;
        if (emitter.name.empty() || emitter.name.size() > kParticleEffectMaxStringBytes)
            Add(result, ParticleEffectDiagnosticCode::InvalidValue, base + ".name",
                "emitter name must be non-empty and bounded", emitter.emitterId);
        else if (!IsValidParticleEffectString(emitter.name))
            Add(result, ParticleEffectDiagnosticCode::InvalidUtf8, base + ".name", "emitter name is not UTF-8",
                emitter.emitterId);
        if (!EnumInRange(emitter.simulationSpace, ParticleSimulationSpace::World))
            Add(result, ParticleEffectDiagnosticCode::InvalidEnum, base + ".simulationSpace",
                "simulation space is invalid", emitter.emitterId);
        if (!EnumInRange(emitter.spawn.mode, ParticleSpawnMode::Burst))
            Add(result, ParticleEffectDiagnosticCode::InvalidEnum, base + ".spawn.mode", "spawn mode is invalid",
                emitter.emitterId);
        if (!Finite(emitter.localPosition) || !Finite(emitter.localRotation) ||
            std::abs(LengthSquared(emitter.localRotation) - 1.0F) > 0.001F || !Finite(emitter.localScale) ||
            emitter.localScale.x <= 0.0F || emitter.localScale.y <= 0.0F || emitter.localScale.z <= 0.0F)
            Add(result, ParticleEffectDiagnosticCode::InvalidValue, base + ".transform",
                "emitter transform must be finite and have positive scale", emitter.emitterId);
        if (emitter.maxParticles == 0U || emitter.maxParticles > kParticleEffectMaxCpuParticlesPerEmitter)
            Add(result, ParticleEffectDiagnosticCode::LimitExceeded, base + ".maxParticles",
                "emitter particle capacity is outside the hard limit", emitter.emitterId);
        if (emitter.modules.size() > kParticleEffectMaxModulesPerEmitter)
            Add(result, ParticleEffectDiagnosticCode::LimitExceeded, base + ".moduleCount",
                "module count exceeds the hard limit", emitter.emitterId);
        ValidateCurve(emitter.spawn.rateOverTime, base + ".spawn.rate", result, emitter.emitterId, 0U, true);
        if (std::any_of(emitter.spawn.rateOverTime.keyframes.begin(), emitter.spawn.rateOverTime.keyframes.end(),
                [](const kb::math::CurveKeyframe& key) {
                    return key.value > kParticleEffectMaxContinuousRatePerSecond;
                }))
            Add(result, ParticleEffectDiagnosticCode::LimitExceeded, base + ".spawn.rate",
                "continuous spawn rate exceeds the fixed-step spawn ceiling", emitter.emitterId);
        if (emitter.spawn.bursts.size() > kParticleEffectMaxBursts)
            Add(result, ParticleEffectDiagnosticCode::LimitExceeded, base + ".spawn.burstCount",
                "burst count exceeds the hard limit", emitter.emitterId);
        float previousBurstTime = -1.0F;
        for (std::size_t burstIndex = 0U; burstIndex < emitter.spawn.bursts.size(); ++burstIndex) {
            const ParticleBurstAsset& burst = emitter.spawn.bursts[burstIndex];
            if (!Finite(burst.timeSeconds) || burst.timeSeconds < 0.0F || burst.timeSeconds <= previousBurstTime ||
                burst.count == 0U || burst.count > kParticleEffectMaxSpawnsPerStep)
                Add(result, ParticleEffectDiagnosticCode::InvalidValue,
                    base + ".spawn.burst[" + std::to_string(burstIndex) + "]",
                    "bursts must be ordered and within spawn limits", emitter.emitterId);
            previousBurstTime = burst.timeSeconds;
        }
        if (!Finite(emitter.spawn.lifetimeMin) || !Finite(emitter.spawn.lifetimeMax) ||
            emitter.spawn.lifetimeMin <= 0.0F || emitter.spawn.lifetimeMax < emitter.spawn.lifetimeMin)
            Add(result, ParticleEffectDiagnosticCode::InvalidValue, base + ".spawn.lifetime",
                "lifetime range is invalid", emitter.emitterId);
        if (!Finite(emitter.spawn.speedMin) || !Finite(emitter.spawn.speedMax) || emitter.spawn.speedMin < 0.0F ||
            emitter.spawn.speedMax < emitter.spawn.speedMin || !Finite(emitter.spawn.direction) ||
            LengthSquared(emitter.spawn.direction) <= 0.000001F || !Finite(emitter.spawn.spreadDegrees) ||
            emitter.spawn.spreadDegrees < 0.0F || emitter.spawn.spreadDegrees > 180.0F ||
            !Finite(emitter.spawn.randomization) || emitter.spawn.randomization < 0.0F ||
            emitter.spawn.randomization > 1.0F || !Finite(emitter.spawn.prewarmSeconds) ||
            emitter.spawn.prewarmSeconds < 0.0F ||
            emitter.spawn.prewarmSeconds > kParticleEffectMaxPrewarmSeconds)
            Add(result, ParticleEffectDiagnosticCode::InvalidValue, base + ".spawn",
                "spawn values are outside the supported range", emitter.emitterId);

        std::set<ParticleStableId> moduleIds;
        std::set<ParticleModuleType> singletonTypes;
        ParticleStableId previousModuleId = 0U;
        for (std::size_t moduleIndex = 0U; moduleIndex < emitter.modules.size(); ++moduleIndex) {
            const ParticleModuleAsset& module = emitter.modules[moduleIndex];
            const std::string path = base + ".module[" + std::to_string(moduleIndex) + "]";
            if (module.moduleId == 0U || !moduleIds.insert(module.moduleId).second)
                Add(result, ParticleEffectDiagnosticCode::InvalidStableId, path + ".id",
                    "module id must be unique and non-zero", emitter.emitterId, module.moduleId);
            if (moduleIndex > 0U && module.moduleId <= previousModuleId)
                Add(result, ParticleEffectDiagnosticCode::UnsortedStableId, path + ".id",
                    "module ids must be strictly increasing", emitter.emitterId, module.moduleId);
            previousModuleId = module.moduleId;
            if (!EnumInRange(module.type, ParticleModuleType::SubEmitter))
                Add(result, ParticleEffectDiagnosticCode::InvalidEnum, path + ".type", "module type is invalid",
                    emitter.emitterId, module.moduleId);
            if (!PayloadMatchesType(module))
                Add(result, ParticleEffectDiagnosticCode::InvalidValue, path + ".payload",
                    "module payload does not match its type", emitter.emitterId, module.moduleId);
            if (!IsRepeatableParticleModule(module.type) && !singletonTypes.insert(module.type).second)
                Add(result, ParticleEffectDiagnosticCode::DuplicateModule, path + ".type",
                    "module type may occur only once", emitter.emitterId, module.moduleId);
            std::visit(
                [&](const auto& payload) {
                    using T = std::decay_t<decltype(payload)>;
                    if constexpr (std::is_same_v<T, ParticleInitialVelocityModule>) {
                        if (!Finite(payload.direction) || LengthSquared(payload.direction) <= 0.000001F ||
                            !Finite(payload.speedMin) || !Finite(payload.speedMax) || payload.speedMin < 0.0F ||
                            payload.speedMax < payload.speedMin || !Finite(payload.randomization) ||
                            payload.randomization < 0.0F || payload.randomization > 1.0F ||
                            !Finite(payload.spreadDegrees) || payload.spreadDegrees < 0.0F ||
                            payload.spreadDegrees > 180.0F)
                            Add(result, ParticleEffectDiagnosticCode::InvalidValue, path + ".initialVelocity",
                                "initial velocity values are invalid", emitter.emitterId, module.moduleId);
                    } else if constexpr (std::is_same_v<T, ParticleGravityModule> ||
                                         std::is_same_v<T, ParticleWindModule>) {
                        if (!Finite(payload.acceleration))
                            Add(result, ParticleEffectDiagnosticCode::InvalidValue, path + ".acceleration",
                                "acceleration must be finite", emitter.emitterId, module.moduleId);
                        if constexpr (std::is_same_v<T, ParticleGravityModule>)
                            if (!Finite(payload.sceneGravityScale))
                                Add(result, ParticleEffectDiagnosticCode::InvalidValue, path + ".sceneGravityScale",
                                    "gravity scale must be finite", emitter.emitterId, module.moduleId);
                            else if ((payload.acceleration.x != 0.0F || payload.acceleration.y != 0.0F ||
                                      payload.acceleration.z != 0.0F) &&
                                     payload.sceneGravityScale != 0.0F)
                                Add(result, ParticleEffectDiagnosticCode::InvalidValue, path + ".gravity",
                                    "custom acceleration and scene gravity scale are mutually exclusive",
                                    emitter.emitterId, module.moduleId);
                    } else if constexpr (std::is_same_v<T, ParticleDragModule>) {
                        if (!Finite(payload.coefficient) || payload.coefficient < 0.0F)
                            Add(result, ParticleEffectDiagnosticCode::InvalidValue, path + ".coefficient",
                                "drag must be finite and non-negative", emitter.emitterId, module.moduleId);
                    } else if constexpr (std::is_same_v<T, ParticleColorOverLifeModule>)
                        ValidateGradient(payload.gradient, path + ".gradient", result, emitter.emitterId,
                                         module.moduleId);
                    else if constexpr (std::is_same_v<T, ParticleSizeOverLifeModule> ||
                                       std::is_same_v<T, ParticleAlphaOverLifeModule>)
                        ValidateCurve(payload.curve, path + ".curve", result, emitter.emitterId, module.moduleId, true);
                    else if constexpr (std::is_same_v<T, ParticleCollisionPlaneModule>) {
                        if (!Finite(payload.normal) || LengthSquared(payload.normal) <= 0.000001F ||
                            !Finite(payload.distance) || !Finite(payload.restitution) || !Finite(payload.friction) ||
                            payload.restitution < 0.0F || payload.restitution > 1.0F || payload.friction < 0.0F ||
                            payload.friction > 1.0F || payload.maxEventsPerStep == 0U ||
                            payload.maxEventsPerStep > kParticleEffectMaxEventsPerStep)
                            Add(result, ParticleEffectDiagnosticCode::InvalidValue, path + ".collisionPlane",
                                "collision plane values are invalid", emitter.emitterId, module.moduleId);
                    } else if constexpr (std::is_same_v<T, ParticleSubEmitterModule>) {
                        if (payload.targetEmitterId == 0U || payload.count == 0U ||
                            payload.count > kParticleEffectMaxSpawnsPerStep || payload.maxDepth == 0U ||
                            payload.maxDepth > kParticleEffectMaxSubEmitterDepth ||
                            !EnumInRange(payload.trigger, ParticleEventTrigger::Collision))
                            Add(result, ParticleEffectDiagnosticCode::InvalidReference, path + ".subEmitter",
                                "sub-emitter reference or limits are invalid", emitter.emitterId, module.moduleId);
                    }
                },
                module.payload);
        }
        if (!OutputPayloadMatchesType(emitter.output))
            Add(result, ParticleEffectDiagnosticCode::InvalidValue, base + ".output.payload",
                "output payload does not match its type", emitter.emitterId);
        if (!EnumInRange(emitter.output.type, ParticleOutputType::Volumetric))
            Add(result, ParticleEffectDiagnosticCode::InvalidEnum, base + ".output.type", "output type is invalid",
                emitter.emitterId);
        if (!EnumInRange(emitter.output.blend, ParticleBlendMode::Premultiplied))
            Add(result, ParticleEffectDiagnosticCode::InvalidEnum, base + ".output.blend", "blend mode is invalid",
                emitter.emitterId);
        if (!EnumInRange(emitter.output.sort, ParticleSortMode::Age))
            Add(result, ParticleEffectDiagnosticCode::InvalidEnum, base + ".output.sort", "sort mode is invalid",
                emitter.emitterId);
        if (!EnumInRange(emitter.output.alignment, ParticleAlignment::Local))
            Add(result, ParticleEffectDiagnosticCode::InvalidEnum, base + ".output.alignment", "alignment is invalid",
                emitter.emitterId);
        ValidateReference(emitter.output.material, base + ".output.material", result, emitter.emitterId);
        ValidateReference(emitter.output.mesh, base + ".output.mesh", result, emitter.emitterId);
        ValidateReference(emitter.output.textureAtlas, base + ".output.textureAtlas", result, emitter.emitterId);
        if (emitter.output.material.Empty())
            Add(result, ParticleEffectDiagnosticCode::InvalidReference, base + ".output.material",
                "output material reference is required", emitter.emitterId);
        if (emitter.output.type == ParticleOutputType::Mesh && emitter.output.mesh.Empty())
            Add(result, ParticleEffectDiagnosticCode::InvalidReference, base + ".output.mesh",
                "mesh output requires a mesh reference", emitter.emitterId);
        if (emitter.output.type != ParticleOutputType::Mesh && !emitter.output.mesh.Empty())
            Add(result, ParticleEffectDiagnosticCode::InvalidReference, base + ".output.mesh",
                "mesh reference is only valid for mesh output", emitter.emitterId);
        const bool supportsFlipbook = emitter.output.type == ParticleOutputType::Billboard ||
                                      emitter.output.type == ParticleOutputType::StretchedBillboard ||
                                      emitter.output.type == ParticleOutputType::PointSprite;
        if (!supportsFlipbook && !emitter.output.textureAtlas.Empty())
            Add(result, ParticleEffectDiagnosticCode::InvalidReference, base + ".output.textureAtlas",
                "texture atlas is only valid for flipbook-capable output", emitter.emitterId);
        if (const auto* billboard = std::get_if<ParticleBillboardOutput>(&emitter.output.payload))
            ValidateFlipbook(billboard->flipbook, emitter.output.textureAtlas, base + ".output.billboard.flipbook",
                             result, emitter.emitterId);
        if (const auto* stretched = std::get_if<ParticleStretchedBillboardOutput>(&emitter.output.payload)) {
            ValidateFlipbook(stretched->flipbook, emitter.output.textureAtlas,
                             base + ".output.stretchedBillboard.flipbook", result, emitter.emitterId);
            if (!Finite(stretched->velocityScale) || stretched->velocityScale < 0.0F ||
                !Finite(stretched->minimumLength) || stretched->minimumLength < 0.0F)
                Add(result, ParticleEffectDiagnosticCode::InvalidValue, base + ".output.stretchedBillboard",
                    "stretched billboard values are invalid", emitter.emitterId);
        }
        if (const auto* point = std::get_if<ParticlePointSpriteOutput>(&emitter.output.payload)) {
            ValidateFlipbook(point->flipbook, emitter.output.textureAtlas, base + ".output.pointSprite.flipbook",
                             result, emitter.emitterId);
            if (!Finite(point->diameter) || point->diameter <= 0.0F)
                Add(result, ParticleEffectDiagnosticCode::InvalidValue, base + ".output.pointSprite.diameter",
                    "point sprite diameter must be finite and positive", emitter.emitterId);
        }
        if (const auto* mesh = std::get_if<ParticleMeshOutput>(&emitter.output.payload); mesh && !Finite(mesh->lodBias))
            Add(result, ParticleEffectDiagnosticCode::InvalidValue, base + ".output.mesh.lodBias",
                "mesh LOD bias must be finite", emitter.emitterId);
        if (const auto* trail = std::get_if<ParticleTrailOutput>(&emitter.output.payload);
            trail &&
            (!Finite(trail->sampleIntervalSeconds) || trail->sampleIntervalSeconds <= 0.0F ||
             !Finite(trail->minimumDistance) || trail->minimumDistance < 0.0F || trail->maxSamplesPerParticle == 0U ||
             trail->maxSamplesPerParticle > kParticleEffectMaxTrailSamplesPerParticle || !Finite(trail->width) ||
             trail->width <= 0.0F))
            Add(result, ParticleEffectDiagnosticCode::InvalidValue, base + ".output.trail",
                "trail output values are invalid", emitter.emitterId);
        if (const auto* ribbon = std::get_if<ParticleRibbonOutput>(&emitter.output.payload);
            ribbon && (ribbon->maxSegments == 0U || ribbon->maxSegments > kParticleEffectMaxStripSegmentsPerEmitter ||
                       !Finite(ribbon->width) || ribbon->width <= 0.0F))
            Add(result, ParticleEffectDiagnosticCode::InvalidValue, base + ".output.ribbon",
                "ribbon output values are invalid", emitter.emitterId);
        if (const auto* beam = std::get_if<ParticleBeamOutput>(&emitter.output.payload);
            beam && (!Finite(beam->localEnd) || LengthSquared(beam->localEnd) <= 0.000001F || beam->segments == 0U ||
                     beam->segments > kParticleEffectMaxStripSegmentsPerEmitter || !Finite(beam->width) ||
                     beam->width <= 0.0F || !Finite(beam->noiseAmplitude) || beam->noiseAmplitude < 0.0F ||
                     !Finite(beam->noiseFrequency) || beam->noiseFrequency < 0.0F))
            Add(result, ParticleEffectDiagnosticCode::InvalidValue, base + ".output.beam",
                "beam output values are invalid", emitter.emitterId);
        if (const auto* volume = std::get_if<ParticleVolumetricOutput>(&emitter.output.payload);
            volume && (!Finite(volume->density) || volume->density <= 0.0F || !Finite(volume->radiusScale) ||
                       volume->radiusScale <= 0.0F || volume->lowQualitySteps == 0U ||
                       volume->highQualitySteps < volume->lowQualitySteps || volume->highQualitySteps > 256U))
            Add(result, ParticleEffectDiagnosticCode::InvalidValue, base + ".output.volumetric",
                "volumetric output values are invalid", emitter.emitterId);
    }

    if (totalParticleCapacity > kParticleEffectMaxCpuParticlesPerScene)
        Add(result, ParticleEffectDiagnosticCode::LimitExceeded, "effect.emitterCapacity",
            "combined emitter particle capacity exceeds the CPU scene hard limit");

    for (std::size_t emitterIndex = 0U; emitterIndex < asset.emitters.size(); ++emitterIndex) {
        const ParticleEmitterAsset& emitter = asset.emitters[emitterIndex];
        for (std::size_t moduleIndex = 0U; moduleIndex < emitter.modules.size(); ++moduleIndex) {
            if (const auto* sub = std::get_if<ParticleSubEmitterModule>(&emitter.modules[moduleIndex].payload);
                sub && !emitterIds.contains(sub->targetEmitterId))
                Add(result, ParticleEffectDiagnosticCode::InvalidReference,
                    "effect.emitter[" + std::to_string(emitterIndex) + "].module[" + std::to_string(moduleIndex) +
                        "].targetEmitterId",
                    "sub-emitter target does not exist", emitter.emitterId, emitter.modules[moduleIndex].moduleId);
        }
    }
    for (std::size_t index = 0U; index < asset.eventBindings.size(); ++index) {
        const ParticleEventBindingAsset& binding = asset.eventBindings[index];
        const std::string path = "effect.eventBinding[" + std::to_string(index) + "]";
        if (!emitterIds.contains(binding.sourceEmitterId))
            Add(result, ParticleEffectDiagnosticCode::InvalidReference, path + ".sourceEmitterId",
                "event source emitter does not exist", binding.sourceEmitterId, binding.sourceModuleId);
        if (!EnumInRange(binding.trigger, ParticleEventTrigger::Collision))
            Add(result, ParticleEffectDiagnosticCode::InvalidEnum, path + ".trigger", "event trigger is invalid",
                binding.sourceEmitterId, binding.sourceModuleId);
        if (!EnumInRange(binding.action, ParticleEventAction::EmitEffectAsset))
            Add(result, ParticleEffectDiagnosticCode::InvalidEnum, path + ".action", "event action is invalid",
                binding.sourceEmitterId, binding.sourceModuleId);
        if (binding.sourceModuleId != 0U) {
            const auto emitter =
                std::find_if(asset.emitters.begin(), asset.emitters.end(), [&](const ParticleEmitterAsset& candidate) {
                    return candidate.emitterId == binding.sourceEmitterId;
                });
            const bool moduleExists =
                emitter != asset.emitters.end() &&
                std::any_of(emitter->modules.begin(), emitter->modules.end(), [&](const ParticleModuleAsset& module) {
                    return module.moduleId == binding.sourceModuleId;
                });
            if (!moduleExists)
                Add(result, ParticleEffectDiagnosticCode::InvalidReference, path + ".sourceModuleId",
                    "event source module does not belong to the source emitter", binding.sourceEmitterId,
                    binding.sourceModuleId);
        }
        if (binding.action == ParticleEventAction::EmitTargetEmitter) {
            if (!emitterIds.contains(binding.targetEmitterId))
                Add(result, ParticleEffectDiagnosticCode::InvalidReference, path + ".targetEmitterId",
                    "event target emitter does not exist", binding.sourceEmitterId, binding.sourceModuleId);
            if (!binding.targetEffect.Empty())
                Add(result, ParticleEffectDiagnosticCode::InvalidReference, path + ".targetEffect",
                    "internal emitter action cannot also target an external effect", binding.sourceEmitterId,
                    binding.sourceModuleId);
        }
        if (binding.action == ParticleEventAction::EmitEffectAsset) {
            if (binding.targetEffect.Empty())
                Add(result, ParticleEffectDiagnosticCode::InvalidReference, path + ".targetEffect",
                    "external effect action requires an asset reference", binding.sourceEmitterId,
                    binding.sourceModuleId);
            if (binding.targetEmitterId != 0U)
                Add(result, ParticleEffectDiagnosticCode::InvalidReference, path + ".targetEmitterId",
                    "external effect action cannot also target an emitter", binding.sourceEmitterId,
                    binding.sourceModuleId);
        }
        ValidateReference(binding.targetEffect, path + ".targetEffect", result, binding.sourceEmitterId,
                          binding.sourceModuleId);
        if (binding.count == 0U || binding.count > kParticleEffectMaxSpawnsPerStep || binding.maxDepth == 0U ||
            binding.maxDepth > kParticleEffectMaxSubEmitterDepth || binding.perStepBudget == 0U ||
            binding.perStepBudget > kParticleEffectMaxEventsPerStep)
            Add(result, ParticleEffectDiagnosticCode::LimitExceeded, path, "event binding limits are invalid",
                binding.sourceEmitterId, binding.sourceModuleId);
    }

    struct InternalEdge {
        std::size_t target = 0U;
        std::string path;
        ParticleStableId emitterId = 0U;
        ParticleStableId moduleId = 0U;
        std::uint32_t maxDepth = 1U;
    };
    std::unordered_map<ParticleStableId, std::size_t> emitterIndexById;
    for (std::size_t index = 0U; index < asset.emitters.size(); ++index)
        emitterIndexById.emplace(asset.emitters[index].emitterId, index);
    std::vector<std::vector<InternalEdge>> graph(asset.emitters.size());
    for (std::size_t emitterIndex = 0U; emitterIndex < asset.emitters.size(); ++emitterIndex) {
        const ParticleEmitterAsset& emitter = asset.emitters[emitterIndex];
        for (std::size_t moduleIndex = 0U; moduleIndex < emitter.modules.size(); ++moduleIndex) {
            const auto* sub = std::get_if<ParticleSubEmitterModule>(&emitter.modules[moduleIndex].payload);
            if (sub == nullptr || !emitterIndexById.contains(sub->targetEmitterId))
                continue;
            graph[emitterIndex].push_back(InternalEdge{
                .target = emitterIndexById.at(sub->targetEmitterId),
                .path = "effect.emitter[" + std::to_string(emitterIndex) + "].module[" + std::to_string(moduleIndex) +
                        "].payload.targetEmitterId",
                .emitterId = emitter.emitterId,
                .moduleId = emitter.modules[moduleIndex].moduleId,
                .maxDepth = sub->maxDepth,
            });
        }
    }
    for (std::size_t bindingIndex = 0U; bindingIndex < asset.eventBindings.size(); ++bindingIndex) {
        const ParticleEventBindingAsset& binding = asset.eventBindings[bindingIndex];
        if (binding.action != ParticleEventAction::EmitTargetEmitter ||
            !emitterIndexById.contains(binding.sourceEmitterId) || !emitterIndexById.contains(binding.targetEmitterId))
            continue;
        graph[emitterIndexById.at(binding.sourceEmitterId)].push_back(InternalEdge{
            .target = emitterIndexById.at(binding.targetEmitterId),
            .path = "effect.eventBinding[" + std::to_string(bindingIndex) + "].targetEmitterId",
            .emitterId = binding.sourceEmitterId,
            .moduleId = binding.sourceModuleId,
            .maxDepth = binding.maxDepth,
        });
    }
    std::vector<bool> pathStack(graph.size(), false);
    std::set<std::string> reportedCycles;
    std::function<void(std::size_t)> visitCycles = [&](std::size_t node) {
        pathStack[node] = true;
        for (const InternalEdge& edge : graph[node]) {
            if (pathStack[edge.target]) {
                if (reportedCycles.insert(edge.path).second)
                    Add(result, ParticleEffectDiagnosticCode::CyclicReference, edge.path,
                        "internal emitter graph contains a cycle", edge.emitterId, edge.moduleId);
                continue;
            }
            visitCycles(edge.target);
        }
        pathStack[node] = false;
    };
    for (std::size_t index = 0U; index < graph.size(); ++index)
        visitCycles(index);

    std::set<std::string> reportedDepthOverflows;
    std::function<void(std::size_t, std::uint32_t)> visitDepth = [&](std::size_t node, std::uint32_t depth) {
        for (const InternalEdge& edge : graph[node]) {
            if (depth >= edge.maxDepth) continue;
            if (depth >= kParticleEffectMaxSubEmitterDepth) {
                if (reportedDepthOverflows.insert(edge.path).second)
                    Add(result, ParticleEffectDiagnosticCode::LimitExceeded, edge.path,
                        "internal emitter graph exceeds the supported recursion depth", edge.emitterId, edge.moduleId);
                continue;
            }
            visitDepth(edge.target, depth + 1U);
        }
    };
    for (std::size_t index = 0U; index < graph.size(); ++index)
        visitDepth(index, 0U);
    return result;
}

std::string FormatParticleEffectDiagnostic(const ParticleEffectDiagnostic& diagnostic) {
    std::ostringstream output;
    if (diagnostic.line != 0U)
        output << "line " << diagnostic.line << ": ";
    if (!diagnostic.propertyPath.empty())
        output << diagnostic.propertyPath << ": ";
    if (diagnostic.emitterId != 0U) {
        output << "[emitterId=" << diagnostic.emitterId;
        if (diagnostic.moduleId != 0U)
            output << ", moduleId=" << diagnostic.moduleId;
        output << "] ";
    }
    output << diagnostic.message;
    return output.str();
}

} // namespace kb::scene
