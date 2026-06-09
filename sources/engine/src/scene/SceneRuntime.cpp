#include "engine/ecs/SystemScheduler.hpp"
#include "engine/scene/SceneSystem.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneRuntimeService.hpp"
#include "scene/SceneState.hpp"
#include "scene/components/SceneComponentRegistry.hpp"
#include "scene/systems/SceneSystemScheduler.hpp"
#include "scene/transform/SceneTransformHierarchySystem.hpp"

#include <utility>

namespace kb::scene {
namespace {

void SynchronizeTransformHierarchy(kb::ecs::World& world, const SceneComponentRegistry& components) {
    SceneTransformHierarchySystem{}.Update(world, components);
}

} // namespace

void SceneRuntimeService::AddSystem(Scene& scene, std::unique_ptr<kb::ecs::System> system) {
    SceneState& state = SceneAccess::State(scene);
    state.systemScheduler.Add(std::move(system), state.world);
}

void SceneRuntimeService::AddSceneSystem(Scene& scene, std::unique_ptr<SceneSystem> system) {
    SceneAccess::State(scene).sceneSystemScheduler.Add(std::move(system), scene);
}

void SceneRuntimeService::SynchronizeTransforms(Scene& scene) {
    SceneState& state = SceneAccess::State(scene);
    SynchronizeTransformHierarchy(state.world, state.components);
}

bool SceneRuntimeService::Update(Scene& scene, float deltaSeconds) {
    SceneState& state = SceneAccess::State(scene);
    SynchronizeTransformHierarchy(state.world, state.components);
    state.sceneSystemScheduler.Update(scene, deltaSeconds);
    state.systemScheduler.Update(state.world, deltaSeconds);
    const bool progressed = state.world.Progress(deltaSeconds);
    SynchronizeTransformHierarchy(state.world, state.components);
    return progressed;
}

void SceneRuntimeService::RequestQuit(Scene& scene) noexcept {
    SceneAccess::State(scene).world.RequestQuit();
}

bool SceneRuntimeService::ShouldQuit(const Scene& scene) noexcept {
    return SceneAccess::State(scene).world.ShouldQuit();
}

kb::ecs::World& SceneRuntimeService::EcsWorld(Scene& scene) noexcept {
    return SceneAccess::State(scene).world;
}

const kb::ecs::World& SceneRuntimeService::EcsWorld(const Scene& scene) noexcept {
    return SceneAccess::State(scene).world;
}

} // namespace kb::scene
