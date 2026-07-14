#pragma once

#include "engine/scene/SceneTimers.hpp"

#include <cstdint>
#include <vector>

namespace kb::scene {

class Scene;

// LIB-095: the engine-side logic behind Timer.Once/Repeat/Cancel/Pause/
// Resume — private (kb::scene internals), consumed through the public
// SceneTimers facade on Scene, mirroring SceneLoadedContentService's own
// facade/service split.
class SceneTimerService {
public:
    SceneTimerService() = delete;

    [[nodiscard]] static std::uint64_t Once(Scene& scene, float delaySeconds, SceneEntity owner, SceneEntity creator = {}) noexcept;
    [[nodiscard]] static std::uint64_t Repeat(Scene& scene, float intervalSeconds, SceneEntity owner, SceneEntity creator = {}) noexcept;
    [[nodiscard]] static bool Cancel(Scene& scene, std::uint64_t id) noexcept;
    [[nodiscard]] static bool Pause(Scene& scene, std::uint64_t id) noexcept;
    [[nodiscard]] static bool Resume(Scene& scene, std::uint64_t id) noexcept;
    [[nodiscard]] static bool Exists(const Scene& scene, std::uint64_t id) noexcept;
    [[nodiscard]] static SceneEntity Creator(const Scene& scene, std::uint64_t id) noexcept;
    [[nodiscard]] static std::vector<TimerFiredRecord> Advance(Scene& scene, float deltaSeconds);
};

} // namespace kb::scene
