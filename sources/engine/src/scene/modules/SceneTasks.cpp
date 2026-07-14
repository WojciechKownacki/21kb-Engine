#include "engine/scene/SceneTasks.hpp"

#include "scene/SceneTaskService.hpp"

#include <utility>

namespace kb::scene {

SceneTasks::SceneTasks(Scene& scene) noexcept
    : scene_(scene) {}

std::uint64_t SceneTasks::Start(std::function<TaskPollResult(float)> poll, SceneEntity owner) {
    return SceneTaskService::Start(scene_, std::move(poll), owner);
}

bool SceneTasks::Cancel(std::uint64_t id) noexcept {
    return SceneTaskService::Cancel(scene_, id);
}

bool SceneTasks::Exists(std::uint64_t id) const noexcept {
    return SceneTaskService::Exists(scene_, id);
}

std::vector<TaskCompletionRecord> SceneTasks::Advance(float deltaSeconds) {
    return SceneTaskService::Advance(scene_, deltaSeconds);
}

} // namespace kb::scene
