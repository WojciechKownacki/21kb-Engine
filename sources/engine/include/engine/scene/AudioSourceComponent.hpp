#pragma once

#include "engine/audio/AudioSettings.hpp"

#include <cstdint>

namespace kb::scene {

struct AudioSourceComponent {
    std::uint64_t clipAssetId = 0;
    float volume = 1.0F;
    float pitch = 1.0F;
    bool loop = false;
    bool spatial = true;
    bool autoplay = false;
    bool enabled = true;
    bool mute = false;
    float pan = 0.0F;
    float spatialBlend = 1.0F;
    kb::audio::AudioAttenuationModel attenuationModel = kb::audio::AudioAttenuationModel::Inverse;
    float minDistance = 1.0F;
    float maxDistance = 500.0F;
    float rolloff = 1.0F;
    float dopplerFactor = 1.0F;
};

} // namespace kb::scene
