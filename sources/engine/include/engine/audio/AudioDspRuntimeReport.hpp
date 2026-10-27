#pragma once

#include "engine/audio/AudioDspEffect.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace kb::audio {

struct AudioDspBusReport {
    std::string bus;
    bool initialized = false;
    bool settingsCurrent = false;
    std::size_t activeEffects = 0U;
    std::vector<AudioDspEffect> appliedEffects;
    std::string error;
};

struct AudioDspRuntimeReport {
    bool backendAvailable = false;
    bool offline = false;
    std::uint32_t sampleRate = 0U;
    std::uint32_t channels = 0U;
    std::uint64_t renderedFrames = 0U;
    std::uint64_t mixerAssetId = 0U;
    std::vector<AudioDspBusReport> buses;
};

inline constexpr std::uint32_t kMaxOfflineAudioFramesPerRead = 384000U;

struct AudioOfflinePcm {
    bool succeeded = false;
    std::uint32_t sampleRate = 0U;
    std::uint32_t channels = 0U;
    std::uint64_t frames = 0U;
    std::vector<float> samples;
    std::string error;
};

} // namespace kb::audio
