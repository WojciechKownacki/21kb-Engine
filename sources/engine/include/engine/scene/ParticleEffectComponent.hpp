#pragma once

#include "engine/scene/ParticleEffectAssetSchema.hpp"

#include <cmath>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace kb::scene {

enum class ParticleOwnerDeathPolicy : std::uint8_t { Drain, Clear };

struct ParticleEffectComponent {
    static constexpr std::string_view StableId = "kb21.rendering.particle-effect";
    static constexpr std::uint32_t SchemaVersion = 1U;

    std::uint64_t effectAssetId = 0U;
    std::uint64_t deterministicSeed = 0U;
    float rateMultiplier = 1.0F;
    std::uint32_t maxParticlesOverride = 0U;
    ParticleOwnerDeathPolicy ownerDeathPolicy = ParticleOwnerDeathPolicy::Drain;
    bool enabled = true;
    bool autoPlay = true;
    bool followTransform = true;
    bool restartOnActivate = true;
};

static_assert(std::is_trivially_copyable_v<ParticleEffectComponent>);

[[nodiscard]] inline bool IsParticleEffectComponentValid(const ParticleEffectComponent& value) noexcept {
    return value.effectAssetId != 0U && std::isfinite(value.rateMultiplier) && value.rateMultiplier > 0.0F &&
           value.rateMultiplier <= 1024.0F &&
           value.maxParticlesOverride <= kParticleEffectMaxCpuParticlesPerEmitter &&
           (value.ownerDeathPolicy == ParticleOwnerDeathPolicy::Drain ||
            value.ownerDeathPolicy == ParticleOwnerDeathPolicy::Clear);
}

[[nodiscard]] inline bool IsParticleEffectComponentPersistable(const ParticleEffectComponent& value) noexcept {
    return IsParticleEffectComponentValid(value) ||
           (!value.enabled && value.effectAssetId == 0U && std::isfinite(value.rateMultiplier) &&
            value.rateMultiplier > 0.0F && value.rateMultiplier <= 1024.0F &&
            value.maxParticlesOverride <= kParticleEffectMaxCpuParticlesPerEmitter &&
            (value.ownerDeathPolicy == ParticleOwnerDeathPolicy::Drain ||
             value.ownerDeathPolicy == ParticleOwnerDeathPolicy::Clear));
}

} // namespace kb::scene
