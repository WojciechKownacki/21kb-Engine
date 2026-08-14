#include "engine/scene/ParticleEffectAssetMigration.hpp"

#include "engine/assets/AssetId.hpp"

#include <algorithm>

namespace kb::scene {
namespace {

constexpr ParticleStableId kLegacyEmitterId = 1U;
constexpr ParticleStableId kLegacyInitialVelocityModuleId = 1U;
constexpr ParticleStableId kLegacyGravityModuleId = 2U;
constexpr ParticleStableId kLegacySizeModuleId = 3U;
constexpr ParticleStableId kLegacyColorModuleId = 4U;

template <typename Payload>
const Payload* FindPayload(const ParticleEmitterAsset& emitter, ParticleModuleType type) noexcept {
    for (const ParticleModuleAsset& module : emitter.modules) {
        if (module.type == type && module.enabled) {
            return std::get_if<Payload>(&module.payload);
        }
    }
    return nullptr;
}

} // namespace

ParticleEffectAsset ParticleEffectAssetMigration::FromLegacy(const LegacyParticleEffectAsset& legacy) {
    ParticleEffectAsset asset{};
    asset.effectId = 1U;
    asset.displayName = "Migrated Particle Effect";
    asset.looping = legacy.looping;
    asset.durationSeconds = legacy.durationSeconds;

    ParticleEmitterAsset emitter{};
    emitter.emitterId = kLegacyEmitterId;
    emitter.name = "Emitter 1";
    emitter.maxParticles = legacy.maxParticles;
    emitter.spawn.rateOverTime.keyframes = {kb::math::CurveKeyframe{
        .time = 0.0F, .value = legacy.emissionRatePerSecond, .easing = kb::math::Easing::Linear}};
    emitter.spawn.lifetimeMin = legacy.startLifetimeMin;
    emitter.spawn.lifetimeMax = legacy.startLifetimeMax;
    emitter.spawn.speedMin = legacy.startSpeedMin;
    emitter.spawn.speedMax = legacy.startSpeedMax;
    emitter.spawn.direction = legacy.direction;
    emitter.spawn.spreadDegrees = legacy.spreadDegrees;

    kb::assets::AssetId materialId{};
    if (kb::assets::TryParseAssetId(legacy.materialReference, materialId) && materialId.IsValid()) {
        emitter.output.material.assetId = materialId.value;
    } else {
        emitter.output.material.virtualPath = legacy.materialReference;
    }

    emitter.modules.push_back(ParticleModuleAsset{
        .moduleId = kLegacyInitialVelocityModuleId,
        .authoringOrder = 0U,
        .type = ParticleModuleType::InitialVelocity,
        .enabled = true,
        .payload =
            ParticleInitialVelocityModule{
                .direction = legacy.direction,
                .speedMin = legacy.startSpeedMin,
                .speedMax = legacy.startSpeedMax,
                .randomization = 1.0F,
                .spreadDegrees = legacy.spreadDegrees,
            },
    });
    emitter.modules.push_back(ParticleModuleAsset{
        .moduleId = kLegacyGravityModuleId,
        .authoringOrder = 1U,
        .type = ParticleModuleType::Gravity,
        .enabled = true,
        .payload = ParticleGravityModule{.acceleration = kb::math::Vec3{},
                                         .sceneGravityScale = legacy.gravityScale},
    });
    kb::math::Curve size = legacy.sizeOverLifetime;
    if (size.keyframes.empty()) {
        size.keyframes.push_back(
            kb::math::CurveKeyframe{.time = 0.0F, .value = 1.0F, .easing = kb::math::Easing::Linear});
    }
    emitter.modules.push_back(ParticleModuleAsset{
        .moduleId = kLegacySizeModuleId,
        .authoringOrder = 2U,
        .type = ParticleModuleType::SizeOverLife,
        .enabled = true,
        .payload = ParticleSizeOverLifeModule{.curve = std::move(size)},
    });
    if (!legacy.colorOverLifetime.stops.empty()) {
        emitter.modules.push_back(ParticleModuleAsset{
            .moduleId = kLegacyColorModuleId,
            .authoringOrder = 3U,
            .type = ParticleModuleType::ColorOverLife,
            .enabled = true,
            .payload = ParticleColorOverLifeModule{.gradient = legacy.colorOverLifetime},
        });
    }
    asset.emitters.push_back(std::move(emitter));
    return asset;
}

ParticleEffectLegacyView BuildParticleEffectLegacyView(const ParticleEffectAsset& asset) noexcept {
    ParticleEffectLegacyView view{};
    if (asset.emitters.empty()) {
        return view;
    }
    const ParticleEmitterAsset& emitter = asset.emitters.front();
    view.emissionRatePerSecond =
        emitter.spawn.rateOverTime.keyframes.empty() ? 0.0F : emitter.spawn.rateOverTime.keyframes.front().value;
    view.startLifetimeMin = emitter.spawn.lifetimeMin;
    view.startLifetimeMax = emitter.spawn.lifetimeMax;
    view.startSpeedMin = emitter.spawn.speedMin;
    view.startSpeedMax = emitter.spawn.speedMax;
    view.direction = emitter.spawn.direction;
    view.spreadDegrees = emitter.spawn.spreadDegrees;
    if (const auto* velocity =
            FindPayload<ParticleInitialVelocityModule>(emitter, ParticleModuleType::InitialVelocity)) {
        view.startSpeedMin = velocity->speedMin;
        view.startSpeedMax = velocity->speedMax;
        view.direction = velocity->direction;
        view.spreadDegrees = velocity->spreadDegrees;
    }
    if (const auto* gravity = FindPayload<ParticleGravityModule>(emitter, ParticleModuleType::Gravity)) {
        view.gravityScale = gravity->sceneGravityScale;
    }
    if (const auto* size = FindPayload<ParticleSizeOverLifeModule>(emitter, ParticleModuleType::SizeOverLife)) {
        view.sizeOverLifetime = &size->curve;
    }
    if (const auto* color = FindPayload<ParticleColorOverLifeModule>(emitter, ParticleModuleType::ColorOverLife)) {
        view.colorOverLifetime = &color->gradient;
    }
    view.looping = asset.looping;
    view.durationSeconds = asset.durationSeconds;
    view.maxParticles = emitter.maxParticles;
    return view;
}

std::string ParticleEffectMaterialReference(const ParticleEffectAsset& asset) {
    if (asset.emitters.empty()) {
        return {};
    }
    const ParticleAssetReference& material = asset.emitters.front().output.material;
    return material.assetId != 0U ? kb::assets::ToString(kb::assets::AssetId{material.assetId}) : material.virtualPath;
}

} // namespace kb::scene
