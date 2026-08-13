#pragma once

#include "engine/scene/ParticleEffectAsset.hpp"

#include <string>

namespace kb::scene {

struct LegacyParticleEffectAsset {
    std::string materialReference;
    bool looping = true;
    float durationSeconds = 5.0F;
    std::uint32_t maxParticles = 256U;
    float emissionRatePerSecond = 10.0F;
    float startSpeedMin = 1.0F;
    float startSpeedMax = 2.0F;
    float startLifetimeMin = 1.0F;
    float startLifetimeMax = 1.0F;
    kb::math::Vec3 direction{0.0F, 1.0F, 0.0F};
    float spreadDegrees = 15.0F;
    float gravityScale = 0.0F;
    kb::math::Curve sizeOverLifetime;
    kb::math::Gradient colorOverLifetime;
};

struct ParticleEffectLegacyView {
    float emissionRatePerSecond = 0.0F;
    float startSpeedMin = 0.0F;
    float startSpeedMax = 0.0F;
    float startLifetimeMin = 0.0F;
    float startLifetimeMax = 0.0F;
    kb::math::Vec3 direction{};
    float spreadDegrees = 0.0F;
    float gravityScale = 0.0F;
    bool looping = false;
    float durationSeconds = 0.0F;
    std::uint32_t maxParticles = 0U;
    const kb::math::Curve* sizeOverLifetime = nullptr;
    const kb::math::Gradient* colorOverLifetime = nullptr;
};

class ParticleEffectAssetMigration final {
  public:
    ParticleEffectAssetMigration() = delete;
    [[nodiscard]] static ParticleEffectAsset FromLegacy(const LegacyParticleEffectAsset& legacy);
};

[[nodiscard]] ParticleEffectLegacyView BuildParticleEffectLegacyView(const ParticleEffectAsset& asset) noexcept;
[[nodiscard]] std::string ParticleEffectMaterialReference(const ParticleEffectAsset& asset);

} // namespace kb::scene
