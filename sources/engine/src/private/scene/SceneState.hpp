#pragma once

#include "engine/assets/AssetManager.hpp"
#include "engine/ecs/SystemScheduler.hpp"
#include "engine/ecs/WorkerPool.hpp"
#include "engine/ecs/World.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneMode.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "scene/components/SceneComponentRegistry.hpp"
#include "scene/components/SceneComponentStorage.hpp"
#include "scene/history/SceneHistoryStack.hpp"
#include "scene/prefab/ScenePrefabInstanceRegistry.hpp"
#include "scene/prefab/ScenePrefabRegistry.hpp"
#include "scene/systems/SceneSystemScheduler.hpp"
#include "scene/transform/SceneTransformBranchUpdater.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct ecs_query_t;

namespace kb::audio {

class IAudioPlaybackBackend;

} // namespace kb::audio

namespace kb::scene {

struct SceneTransformValueCacheEntry {
    SceneEntity entity;
    TransformComponent transform{};
    std::uint64_t cacheVersion = 0U;
    bool valid = false;
    bool dirty = false;
};

struct SceneTransformApplyChunkStats {
    std::size_t updated = 0U;
    std::size_t rootFastPath = 0U;
    std::size_t translatedParentFastPath = 0U;
    std::size_t unrotatedParentFastPath = 0U;
    std::size_t unitScaleParentFastPath = 0U;
    std::size_t uniformScaleParentFastPath = 0U;
    std::size_t staticLocalRotationFastPath = 0U;
};

class SceneState {
public:
    SceneState();
    explicit SceneState(kb::ecs::WorldConfig worldConfig);
    ~SceneState();

    SceneState(const SceneState&) = delete;
    SceneState& operator=(const SceneState&) = delete;
    SceneState(SceneState&&) = delete;
    SceneState& operator=(SceneState&&) = delete;

    kb::ecs::World world;
    SceneComponentRegistry components;
    SceneComponentStorage componentStorage;
    ScenePrefabRegistry prefabs;
    ScenePrefabInstanceRegistry prefabInstances;
    ScenePrefabInstantiationStats lastPrefabInstantiationStats;
    bool suppressPrefabDirtyTracking = false;
    SceneMode mode = SceneMode::Runtime;
    kb::assets::AssetManager assets;
    kb::input::InputSubsystem inputSubsystem;
    SceneHistoryStack undoHistory;
    SceneHistoryStack redoHistory;
    kb::ecs::SystemScheduler systemScheduler;
    SceneSystemScheduler sceneSystemScheduler;
    SceneRuntimeFixedStepSettings fixedStepSettings;
    SceneTransformPropagationBudget transformPropagationBudget;
    float fixedStepAccumulatorSeconds = 0.0F;
    float fixedInterpolationAlpha = 0.0F;
    std::size_t lastFixedStepCount = 0U;
    struct FixedTransformSample {
        TransformComponent previous;
        TransformComponent current;
    };
    std::unordered_map<SceneEntity::IdType, FixedTransformSample> fixedTransformSamples;
    std::unordered_map<SceneEntity::IdType, TransformComponent> fixedTransformStepStart;
    std::vector<std::string> denseEntityNames;
    std::unordered_map<SceneEntity::IdType, std::string> entityNames;
    std::unordered_map<SceneEntity::IdType, std::uint64_t> hierarchyOrder;
    std::vector<std::uint64_t> denseHierarchyOrder;
    std::vector<SceneEntity> hierarchyRoots;
    std::unordered_map<SceneEntity::IdType, SceneEntity> hierarchyParents;
    std::unordered_map<SceneEntity::IdType, std::vector<SceneEntity>> hierarchyChildren;
    std::vector<SceneEntity> denseHierarchyParents;
    std::vector<std::vector<SceneEntity>> denseHierarchyChildren;
    std::vector<std::size_t> prefabHierarchyChildrenPerNodeScratch;
    std::vector<std::vector<SceneEntity>> transformTopologicalBatches;
    std::uint64_t hierarchyTopologyVersion = 1;
    std::uint64_t transformTopologicalBatchesVersion = 0;
    std::uint64_t transformTopologicalBatchBuildCount = 0;
    std::uint64_t transformPropagationCursorVersion = 0;
    std::size_t transformPropagationCursorLevel = 0U;
    std::size_t transformPropagationCursorOffset = 0U;
    std::unordered_map<SceneEntity::IdType, bool> transformDirtyScratch;
    std::unordered_map<SceneEntity::IdType, TransformComponent> transformWorldScratch;
    std::vector<TransformComponent> denseTransformWorldScratch;
    std::vector<std::uint32_t> denseTransformWorldScratchValid;
    std::vector<std::uint8_t> denseTransformDirtyScratch;
    std::uint32_t denseTransformWorldScratchEpoch = 1U;
    std::uint64_t transformValueCacheBuildVersion = 1U;
    std::vector<SceneTransformValueCacheEntry> transformValueDenseScratch;
    std::unordered_map<SceneEntity::IdType, SceneTransformValueCacheEntry> transformValueSparseScratch;
    std::vector<SceneEntity> transformValueCacheLoadEntitiesScratch;
    std::vector<SceneEntity> transformValueCacheLoadDenseMarkedEntities;
    std::vector<std::uint32_t> transformValueCacheLoadDenseMarkEpochs;
    std::unordered_map<SceneEntity::IdType, std::uint32_t> transformValueCacheLoadSparseMarkEpochs;
    std::uint32_t transformValueCacheLoadMarkEpoch = 1U;
    std::vector<SceneTransformBatchEntry> transformHierarchyEntriesScratch;
    std::vector<SceneEntity> transformHierarchyUpdatedEntitiesScratch;
    std::vector<TransformComponent> transformHierarchyUpdatedTransformsScratch;
    std::vector<TransformComponent> transformHierarchyFlushComponentsScratch;
    std::vector<SceneTransformApplyChunkStats> transformHierarchyApplyChunkStatsScratch;
    std::vector<kb::ecs::NativeComponentDirtyRange> transformNativeDirtyRangesScratch;
    std::vector<SceneEntity> transformDirtyFrontierEntities;
    std::vector<SceneEntity> transformDirtyFrontierDenseMarkedEntities;
    std::vector<std::uint32_t> transformDirtyFrontierDenseMarkEpochs;
    std::unordered_map<SceneEntity::IdType, std::uint32_t> transformDirtyFrontierSparseMarkEpochs;
    std::uint32_t transformDirtyFrontierMarkEpoch = 1U;
    std::vector<SceneEntity> transformDirtyFrontierLevelScratch;
    std::vector<SceneEntity> transformDirtyFrontierNextScratch;
    std::vector<std::uint8_t> renderProxyDenseComponentMasks;
    std::unordered_map<SceneEntity::IdType, std::uint8_t> renderProxySparseComponentMasks;
    std::vector<SceneEntity> transformRenderProxyUpdateEntities;
    std::vector<WorldTransformAffine3x4> transformRenderProxyWorldAffine3x4;
    std::vector<std::size_t> transformRenderProxyMeshRendererIndices;
    std::vector<std::size_t> transformRenderProxyVisibleMeshRendererIndices;
    std::vector<std::size_t> transformRenderProxyCameraIndices;
    std::vector<std::size_t> transformRenderProxyLightIndices;
    std::vector<std::size_t> transformRenderProxyIdentityAffineChunkCountsScratch;
    std::size_t lastTransformRenderProxyIdentityAffineFastPathCount = 0U;
    std::unique_ptr<kb::ecs::WorkerPool> transformWorkerPool;
    std::size_t lastTransformHierarchyInspectedCount = 0U;
    std::size_t lastTransformHierarchyUpdatedCount = 0U;
    std::size_t lastTransformHierarchyRootFastPathCount = 0U;
    std::size_t lastTransformHierarchyTranslatedParentFastPathCount = 0U;
    std::size_t lastTransformHierarchyUnrotatedParentFastPathCount = 0U;
    std::size_t lastTransformHierarchyUnitScaleParentFastPathCount = 0U;
    std::size_t lastTransformHierarchyUniformScaleParentFastPathCount = 0U;
    std::size_t lastTransformHierarchyStaticLocalRotationFastPathCount = 0U;
    std::size_t lastTransformHierarchySparseFlushCount = 0U;
    std::size_t lastTransformHierarchyDirtyListFlushCount = 0U;
    std::size_t lastTransformHierarchyDirtyListFlushEntityCount = 0U;
    std::size_t lastTransformHierarchyBatchFlushCount = 0U;
    std::size_t lastTransformHierarchyFlushedEntityCount = 0U;
    std::size_t lastTransformHierarchyDirtyFrontierCount = 0U;
    std::size_t lastTransformHierarchyParallelBatchCount = 0U;
    std::size_t lastTransformHierarchyParallelChunkCount = 0U;
    std::size_t lastTransformHierarchyParallelEntityCount = 0U;
    std::size_t lastTransformHierarchyWorkerCount = 1U;
    std::size_t lastTransformHierarchyParallelFlushCount = 0U;
    std::size_t lastTransformHierarchyParallelFlushChunkCount = 0U;
    std::size_t lastTransformHierarchyParallelFlushEntityCount = 0U;
    std::size_t lastTransformHierarchyParallelFlushWorkerCount = 1U;
    std::uint64_t lastTransformHierarchyCacheBuildNanoseconds = 0U;
    std::uint64_t lastTransformHierarchyEntryBuildNanoseconds = 0U;
    std::uint64_t lastTransformHierarchyKernelApplyNanoseconds = 0U;
    std::uint64_t lastTransformHierarchyFrontierAppendNanoseconds = 0U;
    std::uint64_t lastTransformHierarchyPropagateNanoseconds = 0U;
    std::uint64_t lastTransformHierarchyFlushWriteNanoseconds = 0U;
    std::uint64_t lastTransformHierarchyBackendMarkNanoseconds = 0U;
    std::uint64_t lastTransformHierarchyUpdateNanoseconds = 0U;
    std::uint64_t lastTransformHierarchyFlushNanoseconds = 0U;
    bool lastTransformHierarchyBudgetExhausted = false;
    mutable ecs_query_t* cameraIterationQuery = nullptr;
    mutable ecs_query_t* lightIterationQuery = nullptr;
    mutable ecs_query_t* meshRendererIterationQuery = nullptr;
    mutable ecs_query_t* visibleMeshRendererIterationQuery = nullptr;
    mutable ecs_query_t* physicsBodyIterationQuery = nullptr;
    std::uint64_t nextHierarchyOrder = 1;
    kb::audio::IAudioPlaybackBackend* audioPlaybackBackend = nullptr;
    bool basicLightingEnabled = false;
};

} // namespace kb::scene
