#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TimelineAudioTrack.hpp"

#include <cstdint>
#include <string>

namespace kb::scene {

class Scene;

struct TimelineAudioVoice {
    std::uint64_t id = 0;
    bool paused = false;
};

class TimelineAudioPlayback final {
public:
    static void Stop(Scene& scene, TimelineAudioVoice& voice) noexcept;
    // Scrubbing and reverse traversal are silent. Forward playback resumes at
    // the sampled offset. The caller marks seeks, wraps and rebinding as jumps.
    [[nodiscard]] static bool Synchronize(Scene& scene, const TimelineAudioTrack& track,
        std::uint64_t clipAssetId, SceneEntity target, float timeSeconds, float rate,
        bool playing, bool discontinuity, TimelineAudioVoice& voice, std::string& error);
};

} // namespace kb::scene
