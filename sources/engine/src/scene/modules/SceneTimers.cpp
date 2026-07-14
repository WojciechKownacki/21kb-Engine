#include "engine/scene/SceneTimers.hpp"

#include "scene/SceneTimerService.hpp"

namespace kb::scene {

SceneTimers::SceneTimers(Scene& scene) noexcept
    : scene_(scene) {}

std::uint64_t SceneTimers::Once(float delaySeconds, SceneEntity owner) noexcept {
    return SceneTimerService::Once(scene_, delaySeconds, owner);
}

std::uint64_t SceneTimers::Repeat(float intervalSeconds, SceneEntity owner) noexcept {
    return SceneTimerService::Repeat(scene_, intervalSeconds, owner);
}

bool SceneTimers::Cancel(std::uint64_t id) noexcept {
    return SceneTimerService::Cancel(scene_, id);
}

bool SceneTimers::Pause(std::uint64_t id) noexcept {
    return SceneTimerService::Pause(scene_, id);
}

bool SceneTimers::Resume(std::uint64_t id) noexcept {
    return SceneTimerService::Resume(scene_, id);
}

bool SceneTimers::Exists(std::uint64_t id) const noexcept {
    return SceneTimerService::Exists(scene_, id);
}

std::vector<TimerFiredRecord> SceneTimers::Advance(float deltaSeconds) {
    return SceneTimerService::Advance(scene_, deltaSeconds);
}

} // namespace kb::scene
