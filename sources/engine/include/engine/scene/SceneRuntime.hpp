#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace kb::ecs {

class System;
struct SystemSchedulerTrace;
class World;

} // namespace kb::ecs

namespace kb::scene {

class Scene;
class SceneSystem;

struct SceneRuntimeFixedStepSettings {
    float fixedDeltaSeconds = 1.0F / 60.0F;
    float maxFrameDeltaSeconds = 0.25F;
    std::size_t maxFixedStepsPerFrame = 8U;
};

struct SceneTransformPropagationBudget {
    std::size_t maxInspectedEntitiesPerSync = 0U;
};

struct SceneRuntimeHotPathReport {
    bool transformHierarchyUsesBatchPath = false;
    bool transformHierarchyUsesKernelContract = false;
    bool transformHierarchyUsesVirtualSceneSystem = true;
    std::size_t transformTopologicalBatchCount = 0U;
    std::uint64_t transformTopologicalBatchBuildCount = 0U;
    std::size_t transformRenderProxyUpdateCount = 0U;
    std::size_t transformRenderProxyMeshRendererCount = 0U;
    std::size_t transformRenderProxyVisibleMeshRendererCount = 0U;
    std::size_t transformRenderProxyCameraCount = 0U;
    std::size_t transformRenderProxyLightCount = 0U;
    std::size_t transformRenderProxyIdentityAffineFastPathCount = 0U;
    std::size_t transformHierarchyInspectedCount = 0U;
    std::size_t transformHierarchyUpdatedCount = 0U;
    std::size_t transformHierarchyRootFastPathCount = 0U;
    std::size_t transformHierarchyTranslatedParentFastPathCount = 0U;
    std::size_t transformHierarchyUnrotatedParentFastPathCount = 0U;
    std::size_t transformHierarchyUnitScaleParentFastPathCount = 0U;
    std::size_t transformHierarchyUniformScaleParentFastPathCount = 0U;
    std::size_t transformHierarchyStaticLocalRotationFastPathCount = 0U;
    std::size_t transformHierarchySparseFlushCount = 0U;
    std::size_t transformHierarchyDirtyListFlushCount = 0U;
    std::size_t transformHierarchyDirtyListFlushEntityCount = 0U;
    std::size_t transformHierarchyBatchFlushCount = 0U;
    std::size_t transformHierarchyFlushedEntityCount = 0U;
    std::size_t transformHierarchyDirtyFrontierCount = 0U;
    std::size_t transformHierarchyParallelBatchCount = 0U;
    std::size_t transformHierarchyParallelChunkCount = 0U;
    std::size_t transformHierarchyParallelEntityCount = 0U;
    std::size_t transformHierarchyWorkerCount = 1U;
    std::size_t transformHierarchyParallelFlushCount = 0U;
    std::size_t transformHierarchyParallelFlushChunkCount = 0U;
    std::size_t transformHierarchyParallelFlushEntityCount = 0U;
    std::size_t transformHierarchyParallelFlushWorkerCount = 1U;
    std::uint64_t transformHierarchyCacheBuildNanoseconds = 0U;
    std::uint64_t transformHierarchyEntryBuildNanoseconds = 0U;
    std::uint64_t transformHierarchyKernelApplyNanoseconds = 0U;
    std::uint64_t transformHierarchyFrontierAppendNanoseconds = 0U;
    std::uint64_t transformHierarchyPropagateNanoseconds = 0U;
    std::uint64_t transformHierarchyFlushWriteNanoseconds = 0U;
    std::uint64_t transformHierarchyBackendMarkNanoseconds = 0U;
    std::uint64_t transformHierarchyUpdateNanoseconds = 0U;
    std::uint64_t transformHierarchyFlushNanoseconds = 0U;
    std::size_t transformHierarchyBudgetLimit = 0U;
    bool transformHierarchyBudgetExhausted = false;
};

class SceneRuntimeQueries {
public:
    explicit SceneRuntimeQueries(const Scene& scene) noexcept;

    [[nodiscard]] bool ShouldQuit() const noexcept;
    [[nodiscard]] const kb::ecs::World& EcsWorld() const noexcept;
    [[nodiscard]] SceneRuntimeFixedStepSettings FixedStepSettings() const noexcept;
    [[nodiscard]] SceneTransformPropagationBudget TransformPropagationBudget() const noexcept;
    [[nodiscard]] float FixedInterpolationAlpha() const noexcept;
    [[nodiscard]] std::size_t LastFixedStepCount() const noexcept;
    [[nodiscard]] bool EcsProfilerEnabled() const noexcept;
    [[nodiscard]] const kb::ecs::SystemSchedulerTrace& LastEcsProfilerTrace() const noexcept;
    [[nodiscard]] SceneRuntimeHotPathReport HotPathReport() const noexcept;
    [[nodiscard]] std::optional<TransformComponent> InterpolatedTransform(SceneEntity entity) const noexcept;
    [[nodiscard]] std::span<const SceneEntity> TransformRenderProxyUpdateEntities() const noexcept;
    [[nodiscard]] std::span<const WorldTransformAffine3x4> TransformRenderProxyWorldAffine3x4() const noexcept;
    [[nodiscard]] std::span<const SceneEntity> MeshRendererRenderProxyUpdateEntities() const noexcept;
    // LIB-065: FrameIndex/FixedStepIndex are monotonic (never reset,
    // unlike LastFixedStepCount above); IsPlaying defaults to true and is
    // only ever false when something (e.g. an editor) explicitly calls
    // SceneRuntime::SetPlaying(false).
    [[nodiscard]] std::uint64_t FrameIndex() const noexcept;
    [[nodiscard]] std::uint64_t FixedStepIndex() const noexcept;
    // LIB-093: total simulated seconds across the scene's whole lifetime —
    // see the matching doc comment on SceneState::elapsedSeconds
    // (SceneState.hpp) for the exact accumulation contract.
    [[nodiscard]] double ElapsedSeconds() const noexcept;
    [[nodiscard]] bool IsPlaying() const noexcept;
    // LIB-094: script-visible time multiplier — see SceneState::timeScale's
    // own doc comment for the exact scope (applied only at the Time.Delta
    // boundary, never to engine/physics simulation time).
    [[nodiscard]] float TimeScale() const noexcept;

private:
    const Scene& scene_;
};

class SceneRuntime {
public:
    explicit SceneRuntime(Scene& scene) noexcept;

    void AddSystem(std::unique_ptr<kb::ecs::System> system);
    void AddSceneSystem(std::unique_ptr<SceneSystem> system);
    // LIB-089: the world-matrix update timing contract — WHEN
    // TransformComponent::worldPosition/worldRotation/worldScale are
    // recomputed after a local or parent change, and what each consumer can
    // rely on WITHOUT calling this explicitly (confirmed against
    // SceneRuntimeService::Update's actual sync call sites, SceneRuntime.cpp):
    //  - SCRIPTS (kb::script::ScriptRuntimeSceneSystem, registered via
    //    AddSceneSystem and run from Update() below): get exactly ONE
    //    automatic sync per Update() call, BEFORE the scene-system
    //    scheduler runs. Its FixedTick/Tick/LateTick/BeforeRender/
    //    AfterRender phases then run back-to-back inside that single
    //    scheduler pass with NO further automatic sync between them — a
    //    local/parent change made in one phase is only guaranteed visible
    //    via world* to a LATER phase in the SAME Update() call after an
    //    EXPLICIT call to this method. This is the established pattern
    //    kb::library::Transform.SetWorldPose/SetParent(keepWorld)/LookAt
    //    already follow when they need a fresh parent (or self) world
    //    transform.
    //  - PHYSICS (any SceneSystem whose RequiresFixedStep() is true, e.g.
    //    JoltPhysicsSceneSystem): Update() calls this BEFORE and AFTER
    //    EVERY individual fixed step, not just once per Update() call — a
    //    fixed step's read of world* is always fresh, and any local*/
    //    world* it writes is fully propagated through the hierarchy before
    //    the next fixed step (or any later reader) sees it. No manual call
    //    needed here.
    //  - RENDERER (kb::render::EcsRenderSceneSynchronizer and everything
    //    it feeds, e.g. Renderer::SubmitSceneToViewport): does NOT call
    //    this method itself, but it does NOT blindly trust the caller's
    //    world* cache either. Its transform reader takes a FAST PATH that
    //    consumes the precomputed TransformComponent::world* when that
    //    entry is clean, and OTHERWISE FALLS BACK to a full recursive
    //    resolve from local + parent hierarchy — so it produces a CORRECT
    //    world transform even for an entity whose world* was never synced
    //    (worldDirty). The two paths are counted separately as
    //    EcsRenderSceneSynchronizerStats::transformPrecomputedReadCount vs
    //    transformResolvedFallbackCount; RenderSceneSyncTests exercises
    //    both — the fast path for clean transforms, and (RunSyncFallsBack
    //    ToResolveForDirtyTransformTest) the fallback producing the right
    //    world X for a local-only, never-Update()'d mesh. Consequently,
    //    calling Update()/this method before submission is a PERFORMANCE
    //    contract (it keeps the renderer on the fast path and avoids a
    //    per-submit resolve), NOT a correctness prerequisite. A caller
    //    that drives script execution SEPARATELY from Update() (e.g.
    //    calling a ScriptRuntimeSceneSystem's ExecuteFrame() directly
    //    rather than through AddSceneSystem(), the pattern CliRunCommand.
    //    cpp uses) still submits CORRECT transforms via the fallback;
    //    syncing first only avoids that extra resolve cost.
    void SynchronizeTransforms();
    void SetFixedStepSettings(SceneRuntimeFixedStepSettings settings) noexcept;
    void SetTransformPropagationBudget(SceneTransformPropagationBudget budget) noexcept;
    [[nodiscard]] SceneRuntimeFixedStepSettings FixedStepSettings() const noexcept;
    [[nodiscard]] SceneTransformPropagationBudget TransformPropagationBudget() const noexcept;
    [[nodiscard]] float FixedInterpolationAlpha() const noexcept;
    [[nodiscard]] std::size_t LastFixedStepCount() const noexcept;
    void SetEcsProfilerEnabled(bool enabled) noexcept;
    [[nodiscard]] bool EcsProfilerEnabled() const noexcept;
    [[nodiscard]] const kb::ecs::SystemSchedulerTrace& LastEcsProfilerTrace() const noexcept;
    [[nodiscard]] SceneRuntimeHotPathReport HotPathReport() const noexcept;
    [[nodiscard]] std::optional<TransformComponent> InterpolatedTransform(SceneEntity entity) const noexcept;
    [[nodiscard]] std::span<const SceneEntity> TransformRenderProxyUpdateEntities() const noexcept;
    [[nodiscard]] std::span<const WorldTransformAffine3x4> TransformRenderProxyWorldAffine3x4() const noexcept;
    [[nodiscard]] std::span<const SceneEntity> MeshRendererRenderProxyUpdateEntities() const noexcept;
    // LIB-089: runs one frame. Calls SynchronizeTransforms() (see its own
    // doc comment above for the full per-consumer timing contract)
    // unconditionally before the scene-system scheduler runs, brackets
    // every individual fixed step with a sync before AND after, and syncs
    // once more, unconditionally, after the ECS system scheduler and
    // kb::ecs::World::Progress — so a caller that reads world* or submits
    // to a renderer immediately after this returns always sees fresh data,
    // with no separate manual SynchronizeTransforms() call required.
    [[nodiscard]] bool Update(float deltaSeconds);
    void RequestQuit() noexcept;
    [[nodiscard]] bool ShouldQuit() const noexcept;
    [[nodiscard]] kb::ecs::World& EcsWorld() noexcept;
    [[nodiscard]] std::uint64_t FrameIndex() const noexcept;
    [[nodiscard]] std::uint64_t FixedStepIndex() const noexcept;
    [[nodiscard]] double ElapsedSeconds() const noexcept;
    [[nodiscard]] bool IsPlaying() const noexcept;
    void SetPlaying(bool playing) noexcept;
    [[nodiscard]] float TimeScale() const noexcept;
    void SetTimeScale(float scale) noexcept;

private:
    Scene& scene_;
};

} // namespace kb::scene
