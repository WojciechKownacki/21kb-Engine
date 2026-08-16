#pragma once

#include "engine/scene/ParticleEffectAssetSchema.hpp"

#include <cstdint>
#include <string>

namespace kb::scene {

struct ParticleEffectAsset {
    std::uint32_t formatVersion = kParticleEffectFormatVersion;
    ParticleStableId effectId = 0U;
    std::string displayName;
    std::string recipeCategory;
    std::uint64_t determinismSeed = 0U;
    bool looping = true;
    float durationSeconds = 5.0F;
    ParticleBackendPolicy backendPolicy = ParticleBackendPolicy::CpuDeterministic;
    ParticleGpuCatchupPolicy gpuCatchupPolicy = ParticleGpuCatchupPolicy::RestartFromSeed;
    std::vector<ParticleEmitterAsset> emitters;
    std::vector<ParticleEventBindingAsset> eventBindings;
};

} // namespace kb::scene
