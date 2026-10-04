#include "engine/ecs/SystemScheduler.hpp"
#include "engine/math/EngineMath.hpp"
#include "engine/scene/SceneSystem.hpp"
#include "ecs/GeometricReserve.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneIterationService.hpp"
#include "scene/SceneRuntimeService.hpp"
#include "scene/SceneState.hpp"
#include "scene/SceneTransformService.hpp"
#include "scene/SceneStreamingService.hpp"
#include "scene/components/SceneComponentRegistry.hpp"
#include "scene/systems/SceneSystemScheduler.hpp"
#include "scene/transform/SceneTransformHierarchySystem.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>

namespace kb::scene {
namespace {

void SynchronizeTransformHierarchy(SceneState& state) {
    SceneTransformHierarchySystem{}.Update(state);
    if ((state.fixedTransformTopologyVersion != state.hierarchyTopologyVersion || state.fixedTransformRootAppendEpoch != state.hierarchyRootAppendEpoch) ||
        state.lastTransformHierarchyUpdatedCount == 0U) return;
    // The hierarchy update published the new value of every entity it touched next to the entity itself.
    const bool publishedValues = state.transformHierarchyUpdatedTransformsScratch.size() == state.transformHierarchyUpdatedEntitiesScratch.size();
    // Every updated entity owns its own pose record, so records can be published from several threads; only the list of
    // records touched during a fixed step is shared, and each chunk collects its part of it separately.
    const auto publish = [&state, publishedValues](std::size_t updated, std::vector<std::size_t>& touchedSink) {
        const SceneEntity entity = state.transformHierarchyUpdatedEntitiesScratch[updated];
        std::size_t valueIndex = state.fixedTransformValues.size();
        const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(entity);
        if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex && denseIndex < state.fixedTransformDenseValueIndex.size() &&
            state.fixedTransformDenseValueIndex[denseIndex] != SceneState::kNoFixedTransformValue) {
            valueIndex = state.fixedTransformDenseValueIndex[denseIndex];
        }
        if (valueIndex >= state.fixedTransformValues.size() || state.fixedTransformValues[valueIndex].entity != entity) {
            const auto sample = std::ranges::lower_bound(state.fixedTransformSamples, entity, {}, &SceneState::FixedTransformSample::entity);
            if (sample == state.fixedTransformSamples.end() || sample->entity != entity) return;
            valueIndex = sample->valueIndex;
        }
        const TransformComponent* current = publishedValues ? &state.transformHierarchyUpdatedTransformsScratch[updated]
                                                            : state.componentStorage.Transforms().TryGet(entity);
        if (current == nullptr) return;
        auto& value = state.fixedTransformValues[valueIndex];
        if (state.fixedTransformCapturing) {
            if (!value.touched) {
                value.previous = value.current;
                value.touched = true;
                touchedSink.push_back(valueIndex);
            }
        } else {
            value.previous = *current;
        }
        value.current = *current;
    };
    const std::size_t updatedTotal = state.transformHierarchyUpdatedEntitiesScratch.size();
    constexpr std::size_t kPublishGrainSize = 2048U;
    // The fallback lookups (TryGet, binary search) are read-only too, but only the published-value path is worth the dispatch.
    if (publishedValues && updatedTotal >= kPublishGrainSize * 2U && state.transformWorkerPool != nullptr && state.transformWorkerPool->Running()) {
        const std::size_t chunkCount = (updatedTotal + kPublishGrainSize - 1U) / kPublishGrainSize;
        std::vector<std::vector<std::size_t>> chunkTouched(state.fixedTransformCapturing ? chunkCount : 0U);
        state.transformWorkerPool->ParallelForChunks(updatedTotal, kPublishGrainSize, [&](kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk& chunk) {
            std::vector<std::size_t> localUnused;
            std::vector<std::size_t>& sink = chunkTouched.empty() ? localUnused : chunkTouched[chunk.index];
            for (std::size_t offset = 0U; offset < chunk.count; ++offset) publish(chunk.begin + offset, sink);
        });
        for (const std::vector<std::size_t>& touched : chunkTouched) {
            state.fixedTransformTouched.insert(state.fixedTransformTouched.end(), touched.begin(), touched.end());
        }
        return;
    }
    for (std::size_t updated = 0U; updated < updatedTotal; ++updated) publish(updated, state.fixedTransformTouched);
}

void PublishRuntimeSnapshot(SceneState& state) {
    SceneRuntimeReadSnapshot snapshot;
    snapshot.revision = ++state.runtimeSnapshotRevision;
    snapshot.frameIndex = state.frameIndex;
    snapshot.fixedStepIndex = state.fixedStepIndex;
    snapshot.elapsedSeconds = state.elapsedSeconds;
    snapshot.playing = state.isPlaying;
    snapshot.shouldQuit = state.world.ShouldQuit();
    snapshot.timeScale = state.timeScale;
    state.runtimeSnapshots.Publish(std::move(snapshot));
}

void ApplyRuntimeCommands(SceneState& state) {
    for (const SceneRuntimeCommand& command : state.runtimeCommands.Drain()) {
        switch (command.kind) {
        case SceneRuntimeCommandKind::SetPlaying:
            state.isPlaying = command.playing;
            break;
        case SceneRuntimeCommandKind::SetTimeScale:
            state.timeScale = command.timeScale;
            break;
        case SceneRuntimeCommandKind::RequestQuit:
            state.world.RequestQuit();
            break;
        }
    }
}

[[nodiscard]] std::size_t HierarchyTrackedSlotCount(const SceneState& state) noexcept {
    return std::max(state.hierarchyOrder.size(), state.denseHierarchyOrder.size());
}

[[nodiscard]] Vec3 Lerp(Vec3 from, Vec3 to, float alpha) noexcept {
    return Vec3{
        from.x + (to.x - from.x) * alpha,
        from.y + (to.y - from.y) * alpha,
        from.z + (to.z - from.z) * alpha,
    };
}

// LIB-043: kb::scene::Quat is an alias to kb::math::Quat (TransformComponent.hpp),
// which already provides Normalize — this file's own copy would now be an
// ambiguous overload via ADL against kb::math's.
using kb::math::Normalize;

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

// Keep a pose cache across ticks. Only hierarchy writes refresh it; a static
// world pays for one initial capture instead of two full copies every substep.
void RebuildFixedTransformSamples(Scene& scene, SceneState& state, bool preservePrevious) {
    auto oldSamples = std::move(state.fixedTransformSamples);
    auto oldValues = std::move(state.fixedTransformValues);
    state.fixedTransformSamples.clear();
    state.fixedTransformDenseValueIndex.clear();
    state.fixedTransformValues.clear();
    state.fixedTransformTouched.clear();
    state.fixedTransformSamples.reserve(oldSamples.size());
    state.fixedTransformValues.reserve(oldValues.size());
    struct Context {
        SceneState& state;
        const std::vector<SceneState::FixedTransformSample>& oldSamples;
        const std::vector<SceneState::FixedTransformValues>& oldValues;
        bool preserve;
    } context{state, oldSamples, oldValues, preservePrevious};
    SceneIterationService::ForEachTransform(scene, [](SceneEntity entity, const TransformComponent& current, void* raw) {
        auto& context = *static_cast<Context*>(raw);
        TransformComponent previous = current;
        if (context.preserve) {
            const auto old = std::ranges::lower_bound(context.oldSamples, entity, {}, &SceneState::FixedTransformSample::entity);
            if (old != context.oldSamples.end() && old->entity == entity) {
                const auto& value = context.oldValues[old->valueIndex];
                previous = value.touched ? value.previous : value.current;
            }
        }
        const std::size_t index = context.state.fixedTransformValues.size();
        context.state.fixedTransformSamples.push_back({entity, index});
        context.state.fixedTransformValues.push_back({previous, current, context.preserve, entity});
        if (context.preserve) context.state.fixedTransformTouched.push_back(index);
    }, &context);
    std::ranges::sort(state.fixedTransformSamples, {}, &SceneState::FixedTransformSample::entity);
    state.fixedTransformDenseValueIndex.clear();
    for (std::size_t index = 0U; index < state.fixedTransformValues.size(); ++index) {
        const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(state.fixedTransformValues[index].entity);
        if (denseIndex == kb::ecs::kInvalidGeneratedEntityIndex) continue;
        if (state.fixedTransformDenseValueIndex.size() <= denseIndex) {
            state.fixedTransformDenseValueIndex.resize(static_cast<std::size_t>(denseIndex) + 1U, SceneState::kNoFixedTransformValue);
        }
        state.fixedTransformDenseValueIndex[denseIndex] = static_cast<std::uint32_t>(index);
    }
    state.fixedTransformTopologyVersion = state.hierarchyTopologyVersion;
    state.fixedTransformRootAppendEpoch = state.hierarchyRootAppendEpoch;
}

void CaptureFixedStepStart(Scene& scene, SceneState& state) {
    if ((state.fixedTransformTopologyVersion != state.hierarchyTopologyVersion || state.fixedTransformRootAppendEpoch != state.hierarchyRootAppendEpoch) || state.fixedTransformSamples.empty()) {
        RebuildFixedTransformSamples(scene, state, false);
    }
    for (const std::size_t index : state.fixedTransformTouched) {
        auto& value = state.fixedTransformValues[index];
        value.previous = value.current;
        value.touched = false;
    }
    state.fixedTransformTouched.clear();
    state.fixedTransformCapturing = true;
}

void CaptureFixedStepEnd(Scene& scene, SceneState& state) {
    if ((state.fixedTransformTopologyVersion != state.hierarchyTopologyVersion || state.fixedTransformRootAppendEpoch != state.hierarchyRootAppendEpoch)) {
        RebuildFixedTransformSamples(scene, state, true);
    }
    state.fixedTransformCapturing = false;
}

} // namespace

void SceneRuntimeService::AddSystem(Scene& scene, std::unique_ptr<kb::ecs::System> system) {
    SceneState& state = SceneAccess::State(scene);
    if (state.mode == SceneMode::PrefabPrivate) {
        throw std::logic_error("Cannot register ECS runtime systems in a prefab private scene");
    }
    state.systemScheduler.Add(std::move(system), state.world);
}

SceneSystemHandle SceneRuntimeService::AddSceneSystem(Scene& scene, std::unique_ptr<SceneSystem> system) {
    SceneState& state = SceneAccess::State(scene);
    if (state.mode == SceneMode::PrefabPrivate) {
        throw std::logic_error("Cannot register scene runtime systems in a prefab private scene");
    }
    if (system == nullptr) {
        return {};
    }
    const SceneSystemHandle handle = state.sceneSystemScheduler.Add(std::move(system), scene);
    state.requiresFixedStep = state.sceneSystemScheduler.RequiresFixedStep();
    return handle;
}

bool SceneRuntimeService::RemoveSceneSystem(Scene& scene, SceneSystemHandle handle) noexcept {
    SceneState& state = SceneAccess::State(scene);
    const bool removed = state.sceneSystemScheduler.Remove(handle, scene);
    if (removed) {
        state.requiresFixedStep = state.sceneSystemScheduler.RequiresFixedStep();
    }
    return removed;
}

bool SceneRuntimeService::HasSceneSystem(const Scene& scene, SceneSystemHandle handle) noexcept {
    return SceneAccess::State(scene).sceneSystemScheduler.Contains(handle);
}

std::size_t SceneRuntimeService::SceneSystemCount(const Scene& scene) noexcept {
    return SceneAccess::State(scene).sceneSystemScheduler.Size();
}

std::vector<std::string> SceneRuntimeService::DrainSceneSystemErrors(Scene& scene) {
    return SceneAccess::State(scene).sceneSystemScheduler.DrainSystemErrors();
}

void SceneRuntimeService::ReportSceneSystemError(Scene& scene, std::string message) {
    SceneAccess::State(scene).sceneSystemScheduler.ReportSystemError(std::move(message));
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
    state.fixedTransformDenseValueIndex.clear();
    state.fixedTransformValues.clear();
    state.fixedTransformTouched.clear();
    state.fixedTransformTopologyVersion = 0U;
    state.fixedTransformRootAppendEpoch = 0U;
    state.fixedTransformCapturing = false;
}

SceneRuntimeFixedStepSettings SceneRuntimeService::FixedStepSettings(const Scene& scene) noexcept {
    return SceneAccess::State(scene).fixedStepSettings;
}

void SceneRuntimeService::SetScriptFixedDeltaSeconds(Scene& scene, float seconds) noexcept {
    SceneAccess::State(scene).scriptFixedDeltaSeconds = seconds;
}

float SceneRuntimeService::ScriptFixedDeltaSeconds(const Scene& scene) noexcept {
    return SceneAccess::State(scene).scriptFixedDeltaSeconds;
}

void SceneRuntimeService::SetTransformPropagationBudget(Scene& scene, SceneTransformPropagationBudget budget) noexcept {
    SceneState& state = SceneAccess::State(scene);
    state.transformPropagationBudget = budget;
    state.transformPropagationCursorVersion = 0U;
    state.transformPropagationCursorLevel = 0U;
    state.transformPropagationCursorOffset = 0U;
}

SceneTransformPropagationBudget SceneRuntimeService::TransformPropagationBudget(const Scene& scene) noexcept {
    return SceneAccess::State(scene).transformPropagationBudget;
}

float SceneRuntimeService::FixedInterpolationAlpha(const Scene& scene) noexcept {
    return SceneAccess::State(scene).fixedInterpolationAlpha;
}

std::size_t SceneRuntimeService::LastFixedStepCount(const Scene& scene) noexcept {
    return SceneAccess::State(scene).lastFixedStepCount;
}

std::uint64_t SceneRuntimeService::FrameIndex(const Scene& scene) noexcept {
    return SceneAccess::State(scene).frameIndex;
}

std::uint64_t SceneRuntimeService::FixedStepIndex(const Scene& scene) noexcept {
    return SceneAccess::State(scene).fixedStepIndex;
}

double SceneRuntimeService::ElapsedSeconds(const Scene& scene) noexcept {
    return SceneAccess::State(scene).elapsedSeconds;
}

bool SceneRuntimeService::IsPlaying(const Scene& scene) noexcept {
    return SceneAccess::State(scene).isPlaying;
}

void SceneRuntimeService::SetPlaying(Scene& scene, bool playing) noexcept {
    SceneAccess::State(scene).isPlaying = playing;
}

float SceneRuntimeService::TimeScale(const Scene& scene) noexcept {
    return SceneAccess::State(scene).timeScale;
}

std::shared_ptr<const SceneRuntimeReadSnapshot> SceneRuntimeService::ReadSnapshot(const Scene& scene) {
    return SceneAccess::State(scene).runtimeSnapshots.Read();
}

bool SceneRuntimeService::EnqueueCommand(Scene& scene, SceneRuntimeCommand command) {
    if (command.kind == SceneRuntimeCommandKind::SetTimeScale &&
        (!std::isfinite(command.timeScale) || command.timeScale < 0.0F)) {
        return false;
    }
    SceneAccess::State(scene).runtimeCommands.Enqueue(command);
    return true;
}

void SceneRuntimeService::SetTimeScale(Scene& scene, float scale) noexcept {
    // Defensive clamp only — the script-facing Time.SetScale (ScriptTimeApi.cpp)
    // is the actual validation boundary and rejects negative input with an
    // honest error rather than silently clamping it away (LIB-064's
    // validate-at-the-boundary precedent); this clamp exists purely so no
    // native C++ caller can push the field itself negative.
    SceneAccess::State(scene).timeScale = std::max(0.0F, scale);
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
        .runtimeUpdateNanoseconds = state.lastRuntimeUpdateNanoseconds,
        .runtimeTransformSyncNanoseconds = state.lastRuntimeTransformSyncNanoseconds,
        .runtimeFixedCaptureStartNanoseconds = state.lastRuntimeFixedCaptureStartNanoseconds,
        .runtimeFixedCaptureEndNanoseconds = state.lastRuntimeFixedCaptureEndNanoseconds,
        .transformTopologicalBatchCount = state.transformTopology.Levels().size(),
        .transformTopologicalBatchBuildCount = state.transformTopology.BuildCount(),
        .transformRenderProxyUpdateCount = state.transformRenderProxyUpdateEntities.size(),
        .transformRenderProxyMeshRendererCount = state.transformRenderProxyMeshRendererIndices.size(),
        .transformRenderProxyVisibleMeshRendererCount = state.transformRenderProxyVisibleMeshRendererIndices.size(),
        .transformRenderProxyCameraCount = state.transformRenderProxyCameraIndices.size(),
        .transformRenderProxyLightCount = state.transformRenderProxyLightIndices.size(),
        .transformRenderProxyIdentityAffineFastPathCount = state.lastTransformRenderProxyIdentityAffineFastPathCount,
        .transformHierarchyInspectedCount = state.lastTransformHierarchyInspectedCount,
        .transformHierarchyUpdatedCount = state.lastTransformHierarchyUpdatedCount,
        .transformHierarchyRootFastPathCount = state.lastTransformHierarchyRootFastPathCount,
        .transformHierarchyTranslatedParentFastPathCount = state.lastTransformHierarchyTranslatedParentFastPathCount,
        .transformHierarchyUnrotatedParentFastPathCount = state.lastTransformHierarchyUnrotatedParentFastPathCount,
        .transformHierarchyUnitScaleParentFastPathCount = state.lastTransformHierarchyUnitScaleParentFastPathCount,
        .transformHierarchyUniformScaleParentFastPathCount = state.lastTransformHierarchyUniformScaleParentFastPathCount,
        .transformHierarchyStaticLocalRotationFastPathCount = state.lastTransformHierarchyStaticLocalRotationFastPathCount,
        .transformHierarchySparseFlushCount = state.lastTransformHierarchySparseFlushCount,
        .transformHierarchyDirtyListFlushCount = state.lastTransformHierarchyDirtyListFlushCount,
        .transformHierarchyDirtyListFlushEntityCount = state.lastTransformHierarchyDirtyListFlushEntityCount,
        .transformHierarchyBatchFlushCount = state.lastTransformHierarchyBatchFlushCount,
        .transformHierarchyFlushedEntityCount = state.lastTransformHierarchyFlushedEntityCount,
        .transformHierarchyDirtyFrontierCount = state.lastTransformHierarchyDirtyFrontierCount,
        .transformHierarchyParallelBatchCount = state.lastTransformHierarchyParallelBatchCount,
        .transformHierarchyParallelChunkCount = state.lastTransformHierarchyParallelChunkCount,
        .transformHierarchyParallelEntityCount = state.lastTransformHierarchyParallelEntityCount,
        .transformHierarchyWorkerCount = state.lastTransformHierarchyWorkerCount,
        .transformHierarchyParallelFlushCount = state.lastTransformHierarchyParallelFlushCount,
        .transformHierarchyParallelFlushChunkCount = state.lastTransformHierarchyParallelFlushChunkCount,
        .transformHierarchyParallelFlushEntityCount = state.lastTransformHierarchyParallelFlushEntityCount,
        .transformHierarchyParallelFlushWorkerCount = state.lastTransformHierarchyParallelFlushWorkerCount,
        .transformHierarchyCacheBuildNanoseconds = state.lastTransformHierarchyCacheBuildNanoseconds,
        .transformHierarchyEntryBuildNanoseconds = state.lastTransformHierarchyEntryBuildNanoseconds,
        .transformHierarchyKernelApplyNanoseconds = state.lastTransformHierarchyKernelApplyNanoseconds,
        .transformHierarchyFrontierAppendNanoseconds = state.lastTransformHierarchyFrontierAppendNanoseconds,
        .transformHierarchyPropagateNanoseconds = state.lastTransformHierarchyPropagateNanoseconds,
        .transformHierarchyFlushWriteNanoseconds = state.lastTransformHierarchyFlushWriteNanoseconds,
        .transformHierarchyBackendMarkNanoseconds = state.lastTransformHierarchyBackendMarkNanoseconds,
        .transformHierarchyUpdateNanoseconds = state.lastTransformHierarchyUpdateNanoseconds,
        .transformHierarchyFlushNanoseconds = state.lastTransformHierarchyFlushNanoseconds,
        .transformHierarchyBudgetLimit = state.transformPropagationBudget.maxInspectedEntitiesPerSync,
        .transformHierarchyBudgetExhausted = state.lastTransformHierarchyBudgetExhausted,
        .animatorParallelEvaluationCount = state.lastAnimatorParallelEvaluationCount,
        .animatorParallelWorkerCount = state.lastAnimatorParallelWorkerCount,
        .animatorUpdateRateSkippedPoseCount = state.lastAnimatorUpdateRateSkippedPoseCount,
        .animatorDebugSnapshotAsyncSubmissionCount = state.animatorDebugSnapshotAsyncSubmissionCount,
        .animatorDebugSnapshotSkippedSubmissionCount = state.animatorDebugSnapshotSkippedSubmissionCount,
    };
}

std::optional<TransformComponent> SceneRuntimeService::InterpolatedTransform(const Scene& scene, SceneEntity entity) noexcept {
    const SceneState& state = SceneAccess::State(scene);
    if (!state.world.IsAlive(entity)) return std::nullopt;
    const auto sample = std::ranges::lower_bound(state.fixedTransformSamples, entity, {}, &SceneState::FixedTransformSample::entity);
    if (sample != state.fixedTransformSamples.end() && sample->entity == entity) {
        const auto& values = state.fixedTransformValues[sample->valueIndex];
        return LerpTransform(values.previous, values.current, state.fixedInterpolationAlpha);
    }
    if (const TransformComponent* current = SceneTransformService::TryGet(scene, entity); current != nullptr) {
        return *current;
    }
    return std::nullopt;
}

std::span<const SceneEntity> SceneRuntimeService::TransformRenderProxyUpdateEntities(const Scene& scene) noexcept {
    return SceneAccess::State(scene).transformRenderProxyUpdateEntities;
}

std::span<const WorldTransformAffine3x4> SceneRuntimeService::TransformRenderProxyWorldAffine3x4(const Scene& scene) noexcept {
    return SceneAccess::State(scene).transformRenderProxyWorldAffine3x4;
}

std::span<const SceneEntity> SceneRuntimeService::RenderProxyUpdateEntities(const Scene& scene) noexcept {
    return SceneAccess::State(scene).renderProxyUpdateEntities;
}

std::uint64_t SceneRuntimeService::RenderProxyUpdateRevision(const Scene& scene) noexcept {
    return SceneAccess::State(scene).renderProxyUpdateRevision;
}

std::uint64_t SceneRuntimeService::RenderTopologyVersion(const Scene& scene) noexcept {
    return SceneAccess::State(scene).renderTopologyVersion;
}

bool SceneRuntimeService::Update(Scene& scene, float deltaSeconds) {
    SceneState& state = SceneAccess::State(scene);
    using Clock = std::chrono::steady_clock;
    const auto updateStart = Clock::now();
    const auto nanosecondsSince = [](Clock::time_point start) {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
    };
    state.lastRuntimeTransformSyncNanoseconds = 0U;
    state.lastRuntimeFixedCaptureStartNanoseconds = 0U;
    state.lastRuntimeFixedCaptureEndNanoseconds = 0U;
    const auto synchronizeTransforms = [&state, &nanosecondsSince] {
        const auto start = Clock::now();
        SynchronizeTransformHierarchy(state);
        state.lastRuntimeTransformSyncNanoseconds += nanosecondsSince(start);
    };
    ApplyRuntimeCommands(state);
    // LIB-065: counts every Update() call, unconditionally (including
    // PrefabPrivate scenes below) — "how many times has this scene been
    // stepped" is well-defined regardless of mode.
    ++state.frameIndex;
    // LIB-093: same unconditional convention as frameIndex above — total
    // simulated time is well-defined regardless of scene mode too.
    state.elapsedSeconds += static_cast<double>(deltaSeconds);
    if (state.mode == SceneMode::PrefabPrivate) {
        state.lastFixedStepCount = 0U;
        state.fixedInterpolationAlpha = 0.0F;
        state.transformRenderProxyUpdateEntities.clear();
        state.transformRenderProxyWorldAffine3x4.clear();
        state.transformRenderProxyMeshRendererIndices.clear();
        state.transformRenderProxyVisibleMeshRendererIndices.clear();
        state.transformRenderProxyCameraIndices.clear();
        state.transformRenderProxyLightIndices.clear();
        state.renderProxyUpdateEntities.clear();
        state.renderProxyUpdateEntityIds.clear();
        state.lastTransformRenderProxyIdentityAffineFastPathCount = 0U;
        synchronizeTransforms();
        PublishRuntimeSnapshot(state);
        state.lastRuntimeUpdateNanoseconds = nanosecondsSince(updateStart);
        return false;
    }

    const SceneRuntimeFixedStepSettings fixed = state.fixedStepSettings;
    state.lastFixedStepCount = 0U;
    state.transformRenderProxyUpdateEntities.clear();
    state.transformRenderProxyWorldAffine3x4.clear();
    state.transformRenderProxyMeshRendererIndices.clear();
    state.transformRenderProxyVisibleMeshRendererIndices.clear();
    state.transformRenderProxyCameraIndices.clear();
    state.transformRenderProxyLightIndices.clear();
    state.renderProxyUpdateEntities.clear();
    state.renderProxyUpdateEntityIds.clear();
    state.lastTransformRenderProxyIdentityAffineFastPathCount = 0U;
    kb::ecs::ReserveGeometric(state.transformRenderProxyUpdateEntities, HierarchyTrackedSlotCount(state));
    kb::ecs::ReserveGeometric(state.transformRenderProxyWorldAffine3x4, HierarchyTrackedSlotCount(state));
    kb::ecs::ReserveGeometric(state.transformRenderProxyMeshRendererIndices, HierarchyTrackedSlotCount(state));
    kb::ecs::ReserveGeometric(state.transformRenderProxyVisibleMeshRendererIndices, HierarchyTrackedSlotCount(state));
    kb::ecs::ReserveGeometric(state.transformRenderProxyCameraIndices, HierarchyTrackedSlotCount(state));
    kb::ecs::ReserveGeometric(state.transformRenderProxyLightIndices, HierarchyTrackedSlotCount(state));
    kb::ecs::ReserveGeometric(state.renderProxyUpdateEntities, HierarchyTrackedSlotCount(state));
    state.renderProxyUpdateEntityIds.reserve(HierarchyTrackedSlotCount(state));
    kb::ecs::ReserveGeometric(state.renderProxyDirtyTraversalScratch, HierarchyTrackedSlotCount(state));

    SceneStreamingService::Pump(scene);
    synchronizeTransforms();
    state.sceneSystemScheduler.BeginFrame(scene, deltaSeconds);
    state.sceneSystemScheduler.Update(scene, deltaSeconds, SceneUpdatePhase::PreFixed);

    if (state.isPlaying && state.requiresFixedStep && fixed.fixedDeltaSeconds > 0.0F && fixed.maxFixedStepsPerFrame > 0U) {
        const float clampedDelta = std::clamp(deltaSeconds, 0.0F, std::max(0.0F, fixed.maxFrameDeltaSeconds));
        state.fixedStepAccumulatorSeconds += clampedDelta;
        while (state.fixedStepAccumulatorSeconds >= fixed.fixedDeltaSeconds &&
            state.lastFixedStepCount < fixed.maxFixedStepsPerFrame) {
            state.sceneSystemScheduler.FixedStepBegin(scene, fixed.fixedDeltaSeconds);
            synchronizeTransforms();
            const auto stepStart = Clock::now();
            const auto captureStart = stepStart;
            CaptureFixedStepStart(scene, state);
            state.lastRuntimeFixedCaptureStartNanoseconds += nanosecondsSince(captureStart);
            state.sceneSystemScheduler.FixedUpdate(scene, fixed.fixedDeltaSeconds, SceneFixedUpdatePhase::PreSimulation);
            // FixedTick may flush structural/component commands, including
            // local transform changes. Publish hierarchy-derived world poses
            // before the physics plugin synchronizes its bodies.
            synchronizeTransforms();
            state.sceneSystemScheduler.FixedUpdate(scene, fixed.fixedDeltaSeconds, SceneFixedUpdatePhase::Simulation);
            synchronizeTransforms();
            state.sceneSystemScheduler.FixedUpdate(scene, fixed.fixedDeltaSeconds, SceneFixedUpdatePhase::PostSimulation);
            synchronizeTransforms();
            const auto captureEnd = Clock::now();
            CaptureFixedStepEnd(scene, state);
            state.lastRuntimeFixedCaptureEndNanoseconds += nanosecondsSince(captureEnd);
            state.fixedStepAccumulatorSeconds -= fixed.fixedDeltaSeconds;
            ++state.lastFixedStepCount;
            ++state.fixedStepIndex;
            // A step slower than real time can never be caught up; further steps
            // would only multiply the frame cost (spiral of death), so drop the debt.
            if (static_cast<double>(nanosecondsSince(stepStart)) * 1e-9 >= static_cast<double>(fixed.fixedDeltaSeconds)) {
                state.fixedStepAccumulatorSeconds = std::min(state.fixedStepAccumulatorSeconds, fixed.fixedDeltaSeconds * 0.5F);
                break;
            }
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
        state.fixedTransformDenseValueIndex.clear();
        state.fixedTransformValues.clear();
        state.fixedTransformTouched.clear();
        state.fixedTransformTopologyVersion = 0U;
        state.fixedTransformRootAppendEpoch = 0U;
        state.fixedTransformCapturing = false;
    }

    // Variable consumers (notably script Tick/LateTick) run after the fixed
    // loop, so they see this Update call's physics write-back. Producers such
    // as input polling already ran in PreFixed and were visible to FixedTick.
    state.sceneSystemScheduler.Update(scene, deltaSeconds, SceneUpdatePhase::PostFixed);
    state.systemScheduler.Update(state.world, deltaSeconds);
    const bool progressed = state.world.Progress(deltaSeconds);
    synchronizeTransforms();
    PublishRuntimeSnapshot(state);
    state.sceneSystemScheduler.EndUpdate(scene, deltaSeconds);
    state.lastRuntimeUpdateNanoseconds = nanosecondsSince(updateStart);
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
