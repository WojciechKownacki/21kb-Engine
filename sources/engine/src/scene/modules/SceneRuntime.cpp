#include "engine/scene/SceneRuntime.hpp"

#include "engine/ecs/System.hpp"
#include "engine/scene/SceneSystem.hpp"
#include "scene/SceneRuntimeService.hpp"

#include <utility>

namespace kb::scene {

SceneRuntimeQueries::SceneRuntimeQueries(const Scene& scene) noexcept
    : scene_(scene) {}

bool SceneRuntimeQueries::ShouldQuit() const noexcept {
    return SceneRuntimeService::ShouldQuit(scene_);
}

const kb::ecs::World& SceneRuntimeQueries::EcsWorld() const noexcept {
    return SceneRuntimeService::EcsWorld(scene_);
}

SceneRuntime::SceneRuntime(Scene& scene) noexcept
    : scene_(scene) {}

void SceneRuntime::AddSystem(std::unique_ptr<kb::ecs::System> system) {
    SceneRuntimeService::AddSystem(scene_, std::move(system));
}

void SceneRuntime::AddSceneSystem(std::unique_ptr<SceneSystem> system) {
    SceneRuntimeService::AddSceneSystem(scene_, std::move(system));
}

void SceneRuntime::SynchronizeTransforms() {
    SceneRuntimeService::SynchronizeTransforms(scene_);
}

bool SceneRuntime::Update(float deltaSeconds) {
    return SceneRuntimeService::Update(scene_, deltaSeconds);
}

void SceneRuntime::RequestQuit() noexcept {
    SceneRuntimeService::RequestQuit(scene_);
}

bool SceneRuntime::ShouldQuit() const noexcept {
    return SceneRuntimeService::ShouldQuit(scene_);
}

kb::ecs::World& SceneRuntime::EcsWorld() noexcept {
    return SceneRuntimeService::EcsWorld(scene_);
}

} // namespace kb::scene
