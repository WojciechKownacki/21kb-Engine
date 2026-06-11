#include "engine/ecs/SystemScheduler.hpp"
#include "engine/scene/SceneSystem.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneRuntimeService.hpp"
#include "scene/SceneState.hpp"
#include "scene/components/SceneComponentRegistry.hpp"
#include "scene/systems/SceneSystemScheduler.hpp"
#include "scene/transform/SceneTransformHierarchySystem.hpp"

#include <algorithm>
#include <cstddef>
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

void SceneRuntimeService::SetFixedStepSettings(Scene& scene, SceneRuntimeFixedStepSettings settings) noexcept {
    SceneState& state = SceneAccess::State(scene);
    state.fixedStepSettings = settings;
    state.fixedStepAccumulatorSeconds = 0.0F;
    state.fixedInterpolationAlpha = 0.0F;
    state.lastFixedStepCount = 0U;
}

SceneRuntimeFixedStepSettings SceneRuntimeService::FixedStepSettings(const Scene& scene) noexcept {
    return SceneAccess::State(scene).fixedStepSettings;
}

float SceneRuntimeService::FixedInterpolationAlpha(const Scene& scene) noexcept {
    return SceneAccess::State(scene).fixedInterpolationAlpha;
}

std::size_t SceneRuntimeService::LastFixedStepCount(const Scene& scene) noexcept {
    return SceneAccess::State(scene).lastFixedStepCount;
}

bool SceneRuntimeService::Update(Scene& scene, float deltaSeconds) {
    SceneState& state = SceneAccess::State(scene);
    const SceneRuntimeFixedStepSettings fixed = state.fixedStepSettings;
    state.lastFixedStepCount = 0U;

    SynchronizeTransformHierarchy(state.world, state.components);
    state.sceneSystemScheduler.Update(scene, deltaSeconds);

    if (fixed.fixedDeltaSeconds > 0.0F && fixed.maxFixedStepsPerFrame > 0U) {
        const float clampedDelta = std::clamp(deltaSeconds, 0.0F, std::max(0.0F, fixed.maxFrameDeltaSeconds));
        state.fixedStepAccumulatorSeconds += clampedDelta;
        while (state.fixedStepAccumulatorSeconds >= fixed.fixedDeltaSeconds &&
            state.lastFixedStepCount < fixed.maxFixedStepsPerFrame) {
            SynchronizeTransformHierarchy(state.world, state.components);
            state.sceneSystemScheduler.FixedUpdate(scene, fixed.fixedDeltaSeconds);
            state.fixedStepAccumulatorSeconds -= fixed.fixedDeltaSeconds;
            ++state.lastFixedStepCount;
        }
        if (state.lastFixedStepCount == fixed.maxFixedStepsPerFrame &&
            state.fixedStepAccumulatorSeconds >= fixed.fixedDeltaSeconds) {
            state.fixedStepAccumulatorSeconds = 0.0F;
        }
        state.fixedInterpolationAlpha = std::clamp(state.fixedStepAccumulatorSeconds / fixed.fixedDeltaSeconds, 0.0F, 1.0F);
    } else {
        state.fixedStepAccumulatorSeconds = 0.0F;
        state.fixedInterpolationAlpha = 0.0F;
    }

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
