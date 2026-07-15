#pragma once

#include "engine/scene/SceneTasks.hpp"

#include <cstddef>
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

    [[nodiscard]] static std::uint64_t Start(Scene& scene, std::function<TaskPollResult(float)> poll, SceneEntity owner, SceneEntity creator = {});
    // LIB-098: identical to Start, but marks the record fixedStepDomain —
    // see SceneTasks.hpp's StartFixedStep doc comment.
    [[nodiscard]] static std::uint64_t StartFixedStep(Scene& scene, std::function<TaskPollResult(float)> poll, SceneEntity owner, SceneEntity creator = {});
    [[nodiscard]] static bool Cancel(Scene& scene, std::uint64_t id) noexcept;
    [[nodiscard]] static bool Exists(const Scene& scene, std::uint64_t id) noexcept;
    [[nodiscard]] static SceneEntity Creator(const Scene& scene, std::uint64_t id) noexcept;
    // Only polls Frame-domain (Start) tasks.
    [[nodiscard]] static std::vector<TaskCompletionRecord> Advance(Scene& scene, float deltaSeconds);
    // LIB-098: only polls FixedTick-domain (StartFixedStep) tasks.
    [[nodiscard]] static std::vector<TaskCompletionRecord> AdvanceFixedSteps(Scene& scene, std::size_t stepCount);
};

} // namespace kb::scene
