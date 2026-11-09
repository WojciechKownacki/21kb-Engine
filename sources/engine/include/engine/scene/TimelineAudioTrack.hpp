#pragma once

#include <string>

namespace kb::scene {

struct TimelineAudioTrack {
    std::string binding;
    std::string clipReference;
    float startSeconds = 0.0F;
    float endSeconds = 1.0F;
    float clipOffsetSeconds = 0.0F;
    float volume = 1.0F;
    bool spatial = false;
    std::string outputBus;
};

[[nodiscard]] bool ValidateTimelineAudioTrack(
    const TimelineAudioTrack& track, float durationSeconds, std::string& error);

} // namespace kb::scene
