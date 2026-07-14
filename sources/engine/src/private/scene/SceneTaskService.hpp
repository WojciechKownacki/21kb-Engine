#pragma once

#include "engine/scene/SceneTasks.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace kb::scene {

class Scene;

// LIB-097: the engine-side logic behind Task.Start (native-only)/Cancel —
// private (kb::scene internals), consumed through the public SceneTasks
// facade on Scene, mirroring SceneTimerService's own facade/service split.
class SceneTaskService {
public:
    SceneTaskService() = delete;

    [[nodiscard]] static std::uint64_t Start(Scene& scene, std::function<TaskPollResult(float)> poll, SceneEntity owner);
    [[nodiscard]] static bool Cancel(Scene& scene, std::uint64_t id) noexcept;
    [[nodiscard]] static bool Exists(const Scene& scene, std::uint64_t id) noexcept;
    [[nodiscard]] static std::vector<TaskCompletionRecord> Advance(Scene& scene, float deltaSeconds);
};

} // namespace kb::scene
