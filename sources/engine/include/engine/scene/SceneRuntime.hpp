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

private:
    const Scene& scene_;
};

class SceneRuntime {
public:
    explicit SceneRuntime(Scene& scene) noexcept;

    void AddSystem(std::unique_ptr<kb::ecs::System> system);
    void AddSceneSystem(std::unique_ptr<SceneSystem> system);
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
    [[nodiscard]] bool Update(float deltaSeconds);
    void RequestQuit() noexcept;
    [[nodiscard]] bool ShouldQuit() const noexcept;
    [[nodiscard]] kb::ecs::World& EcsWorld() noexcept;

private:
    Scene& scene_;
};

} // namespace kb::scene
