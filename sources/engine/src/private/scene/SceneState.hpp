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
#include <unordered_set>
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
    // True once a fixed-step system (e.g. physics) has been added. Scenes without
    // one skip the fixed-step substep loop and its per-substep interpolation
    // sampling, which otherwise rebuilds a map of every transform each substep.
    bool requiresFixedStep = false;
    float fixedStepAccumulatorSeconds = 0.0F;
    float fixedInterpolationAlpha = 0.0F;
    std::size_t lastFixedStepCount = 0U;
    // LIB-065: monotonic counters, never reset (unlike lastFixedStepCount,
    // which is a per-frame count reset at the top of every Update()).
    // frameIndex counts Update() calls; fixedStepIndex counts individual
    // fixed-step substeps across the scene's whole lifetime.
    std::uint64_t frameIndex = 0U;
    std::uint64_t fixedStepIndex = 0U;
    // LIB-065: true by default (a standalone/headless run has no "edit
    // mode" to distinguish from) — an editor sets this false while the
    // scene is being edited rather than simulated, via SetPlaying.
    bool isPlaying = true;
    struct FixedTransformSample {
        TransformComponent previous;
        TransformComponent current;
    };
    std::unordered_map<SceneEntity::IdType, FixedTransformSample> fixedTransformSamples;
    std::unordered_map<SceneEntity::IdType, TransformComponent> fixedTransformStepStart;
    std::vector<std::string> denseEntityNames;
    std::unordered_map<SceneEntity::IdType, std::string> entityNames;
    // LIB-068: entities are active by default (absent from this set); an
    // entity is only ever added here by an explicit SetActive(false) —
    // same "presence means non-default" shape already used by
    // hierarchyParents/hierarchyChildren for the (much more common)
    // default-empty case, so a scene with no deactivated entities pays no
    // per-entity storage cost.
    std::unordered_set<SceneEntity::IdType> inactiveEntities;
    // LIB-072: the persistent/gameplay boundary — a ROOT entity present here
    // survives ClearSceneRoots (SceneDocumentService::LoadIntoScene's
    // non-additive-Scene.Load wipe), along with its whole hierarchy (Destroy
    // cascades to children, so skipping a persistent root preserves its
    // entire subtree without needing separate per-child tracking). Same
    // "presence means non-default" flat-set shape as inactiveEntities.
    // Marking a NON-root entity persistent has no protective effect on its
    // own — ClearSceneRoots only ever evaluates roots, so a persistent
    // child still cascade-destroys if its non-persistent parent's root is
    // cleared (mirrors Unity's DontDestroyOnLoad, which likewise only
    // affects root-level GameObjects); this is an intentional, documented
    // scope limit, not an oversight.
    std::unordered_set<SceneEntity::IdType> persistentEntities;
    // LIB-071: content loaded via Scene.Load (additive or not). Each
    // record names ONE loaded document's own root entity so it can later
    // be selectively Scene.Unload'ed without touching content loaded from
    // a different document. `id` is assigned by SceneLoadedContentService,
    // never reused within a scene's lifetime (nextLoadedSceneId only ever
    // increments), so a stale handle from an already-unloaded record can
    // never collide with a later one.
    struct LoadedSceneRecord {
        std::uint64_t id = 0U;
        std::string name;
        std::string path;
        SceneEntity root{};
    };
    std::vector<LoadedSceneRecord> loadedScenes;
    std::uint64_t nextLoadedSceneId = 1U;
    std::uint64_t activeLoadedSceneId = 0U;
    // LIB-073: scene lifecycle notifications (SceneLoading/SceneLoaded/
    // SceneActivated/SceneUnloading/SceneUnloaded) queued by
    // SceneLoadedContentService — an "unambiguous command deferred to a
    // queue" (Engine21kbLibrary.md's own invariant #3), not a second
    // source of truth: kb::scene never interprets these itself. Drained
    // once per frame by kb::script::ScriptRuntimeSceneSystem via the
    // public SceneLoadedContent::DrainPendingLifecycleEvents() facade and
    // turned into real ScriptEvent broadcasts.
    struct PendingSceneLifecycleEvent {
        std::string name;
        std::uint64_t sceneId = 0U;
        std::string sceneName;
    };
    std::vector<PendingSceneLifecycleEvent> pendingSceneLifecycleEvents;
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
    std::vector<SceneEntity> meshRendererRenderProxyUpdateEntities;
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
