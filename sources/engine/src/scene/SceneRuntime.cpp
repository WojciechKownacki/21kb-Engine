#include "engine/ecs/SystemScheduler.hpp"
#include "engine/scene/SceneSystem.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneIterationService.hpp"
#include "scene/SceneRuntimeService.hpp"
#include "scene/SceneState.hpp"
#include "scene/SceneTransformService.hpp"
#include "scene/components/SceneComponentRegistry.hpp"
#include "scene/systems/SceneSystemScheduler.hpp"
#include "scene/transform/SceneTransformHierarchySystem.hpp"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>

namespace kb::scene {
namespace {

void SynchronizeTransformHierarchy(SceneState& state) {
    SceneTransformHierarchySystem{}.Update(state);
}

[[nodiscard]] Vec3 Lerp(Vec3 from, Vec3 to, float alpha) noexcept {
    return Vec3{
        from.x + (to.x - from.x) * alpha,
        from.y + (to.y - from.y) * alpha,
        from.z + (to.z - from.z) * alpha,
    };
}

[[nodiscard]] Quat Normalize(Quat value) noexcept {
    const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w);
    if (length <= 0.0F) {
        return Quat{};
    }
    const float inverse = 1.0F / length;
    return Quat{ value.x * inverse, value.y * inverse, value.z * inverse, value.w * inverse };
}

[[nodiscard]] Quat Lerp(Quat from, Quat to, float alpha) noexcept {
    const float dot = from.x * to.x + from.y * to.y + from.z * to.z + from.w * to.w;
    if (dot < 0.0F) {
        to = Quat{ -to.x, -to.y, -to.z, -to.w };
    }
    return Normalize(Quat{
        from.x + (to.x - from.x) * alpha,
        from.y + (to.y - from.y) * alpha,
        from.z + (to.z - from.z) * alpha,
        from.w + (to.w - from.w) * alpha,
    });
}

[[nodiscard]] TransformComponent LerpTransform(const TransformComponent& previous, const TransformComponent& current, float alpha) noexcept {
    TransformComponent result = current;
    result.localPosition = Lerp(previous.localPosition, current.localPosition, alpha);
    result.localRotation = Lerp(previous.localRotation, current.localRotation, alpha);
    result.localScale = Lerp(previous.localScale, current.localScale, alpha);
    result.worldPosition = Lerp(previous.worldPosition, current.worldPosition, alpha);
    result.worldRotation = Lerp(previous.worldRotation, current.worldRotation, alpha);
    result.worldScale = Lerp(previous.worldScale, current.worldScale, alpha);
    result.worldDirty = false;
    return result;
}

void CaptureFixedStepStart(Scene& scene, SceneState& state) {
    state.fixedTransformStepStart.clear();
    state.fixedTransformStepStart.reserve(state.fixedTransformSamples.size());
    SceneIterationService::ForEachTransform(scene, [](SceneEntity entity, const TransformComponent& transform, void* context) {
        auto* samples = static_cast<std::unordered_map<SceneEntity::IdType, TransformComponent>*>(context);
        samples->emplace(entity.Id(), transform);
    }, &state.fixedTransformStepStart);
}

void CaptureFixedStepEnd(Scene& scene, SceneState& state) {
    std::unordered_map<SceneEntity::IdType, SceneState::FixedTransformSample> samples;
    samples.reserve(state.fixedTransformStepStart.size());
    std::pair<
        const std::unordered_map<SceneEntity::IdType, TransformComponent>*,
        std::unordered_map<SceneEntity::IdType, SceneState::FixedTransformSample>*>
        context{ &state.fixedTransformStepStart, &samples };
    SceneIterationService::ForEachTransform(scene, [](SceneEntity entity, const TransformComponent& current, void* context) {
        auto* data = static_cast<std::pair<
            const std::unordered_map<SceneEntity::IdType, TransformComponent>*,
            std::unordered_map<SceneEntity::IdType, SceneState::FixedTransformSample>*>*>(context);
        const auto previous = data->first->find(entity.Id());
        data->second->emplace(entity.Id(), SceneState::FixedTransformSample{
            .previous = previous == data->first->end() ? current : previous->second,
            .current = current,
        });
    }, &context);
    state.fixedTransformSamples = std::move(samples);
}

} // namespace

void SceneRuntimeService::AddSystem(Scene& scene, std::unique_ptr<kb::ecs::System> system) {
    SceneState& state = SceneAccess::State(scene);
    if (state.mode == SceneMode::PrefabPrivate) {
        throw std::logic_error("Cannot register ECS runtime systems in a prefab private scene");
    }
    state.systemScheduler.Add(std::move(system), state.world);
}

void SceneRuntimeService::AddSceneSystem(Scene& scene, std::unique_ptr<SceneSystem> system) {
    SceneState& state = SceneAccess::State(scene);
    if (state.mode == SceneMode::PrefabPrivate) {
        throw std::logic_error("Cannot register scene runtime systems in a prefab private scene");
    }
    state.sceneSystemScheduler.Add(std::move(system), scene);
}

void SceneRuntimeService::SynchronizeTransforms(Scene& scene) {
    SceneState& state = SceneAccess::State(scene);
    SynchronizeTransformHierarchy(state);
}

void SceneRuntimeService::SetFixedStepSettings(Scene& scene, SceneRuntimeFixedStepSettings settings) noexcept {
    SceneState& state = SceneAccess::State(scene);
    state.fixedStepSettings = settings;
    state.fixedStepAccumulatorSeconds = 0.0F;
    state.fixedInterpolationAlpha = 0.0F;
    state.lastFixedStepCount = 0U;
    state.fixedTransformSamples.clear();
    state.fixedTransformStepStart.clear();
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

void SceneRuntimeService::SetEcsProfilerEnabled(Scene& scene, bool enabled) noexcept {
    SceneAccess::State(scene).systemScheduler.SetProfilerEnabled(enabled);
}

bool SceneRuntimeService::EcsProfilerEnabled(const Scene& scene) noexcept {
    return SceneAccess::State(scene).systemScheduler.ProfilerEnabled();
}

const kb::ecs::SystemSchedulerTrace& SceneRuntimeService::LastEcsProfilerTrace(const Scene& scene) noexcept {
    return SceneAccess::State(scene).systemScheduler.LastProfilerTrace();
}

SceneRuntimeHotPathReport SceneRuntimeService::HotPathReport(const Scene& scene) noexcept {
    const SceneState& state = SceneAccess::State(scene);
    return SceneRuntimeHotPathReport{
        .transformHierarchyUsesBatchPath = true,
        .transformHierarchyUsesKernelContract = true,
        .transformHierarchyUsesVirtualSceneSystem = false,
        .transformTopologicalBatchCount = state.transformTopologicalBatches.size(),
        .transformRenderProxyUpdateCount = state.transformRenderProxyUpdateEntities.size(),
    };
}

std::optional<TransformComponent> SceneRuntimeService::InterpolatedTransform(const Scene& scene, SceneEntity entity) noexcept {
    const SceneState& state = SceneAccess::State(scene);
    const auto sample = state.fixedTransformSamples.find(entity.Id());
    if (sample != state.fixedTransformSamples.end()) {
        return LerpTransform(sample->second.previous, sample->second.current, state.fixedInterpolationAlpha);
    }
    if (const TransformComponent* current = SceneTransformService::TryGet(scene, entity); current != nullptr) {
        return *current;
    }
    return std::nullopt;
}

std::span<const SceneEntity> SceneRuntimeService::TransformRenderProxyUpdateEntities(const Scene& scene) noexcept {
    return SceneAccess::State(scene).transformRenderProxyUpdateEntities;
}

bool SceneRuntimeService::Update(Scene& scene, float deltaSeconds) {
    SceneState& state = SceneAccess::State(scene);
    if (state.mode == SceneMode::PrefabPrivate) {
        state.lastFixedStepCount = 0U;
        state.fixedInterpolationAlpha = 0.0F;
        state.transformRenderProxyUpdateEntities.clear();
        SynchronizeTransformHierarchy(state);
        return false;
    }

    const SceneRuntimeFixedStepSettings fixed = state.fixedStepSettings;
    state.lastFixedStepCount = 0U;
    state.transformRenderProxyUpdateEntities.clear();
    state.transformRenderProxyUpdateEntities.reserve(state.hierarchyOrder.size());

    SynchronizeTransformHierarchy(state);
    state.sceneSystemScheduler.Update(scene, deltaSeconds);

    if (fixed.fixedDeltaSeconds > 0.0F && fixed.maxFixedStepsPerFrame > 0U) {
        const float clampedDelta = std::clamp(deltaSeconds, 0.0F, std::max(0.0F, fixed.maxFrameDeltaSeconds));
        state.fixedStepAccumulatorSeconds += clampedDelta;
        while (state.fixedStepAccumulatorSeconds >= fixed.fixedDeltaSeconds &&
            state.lastFixedStepCount < fixed.maxFixedStepsPerFrame) {
            SynchronizeTransformHierarchy(state);
            CaptureFixedStepStart(scene, state);
            state.sceneSystemScheduler.FixedUpdate(scene, fixed.fixedDeltaSeconds);
            SynchronizeTransformHierarchy(state);
            CaptureFixedStepEnd(scene, state);
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
        state.fixedTransformSamples.clear();
        state.fixedTransformStepStart.clear();
    }

    state.systemScheduler.Update(state.world, deltaSeconds);
    const bool progressed = state.world.Progress(deltaSeconds);
    SynchronizeTransformHierarchy(state);
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
