#include "scene/transform/SceneTransformHierarchySystem.hpp"

#include "engine/ecs/Query.hpp"
#include "engine/ecs/UnsafeHotQuery.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "ecs/GeometricReserve.hpp"
#include "ecs/world/WorldInternalAccess.hpp"
#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentIteration.hpp"
#include "scene/components/SceneComponentRegistry.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"
#include "scene/SceneRenderProxyComponentMask.hpp"
#include "scene/hierarchy/SceneHierarchyCache.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"
#include "scene/transform/SceneTransformBranchUpdater.hpp"
#include "scene/transform/SceneTransformDirtyFrontier.hpp"
#include "scene/transform/SceneTransformRootHotKernel.hpp"
#include "scene/transform/SceneTransformRootQueryCache.hpp"
#include "scene/transform/TransformMath.hpp"
#include "scene/transform/ExactRootNormalize4.hpp"

#include <algorithm>
#if defined(_M_X64) || defined(__x86_64__)
#include <emmintrin.h>
#endif
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <unordered_map>
#include <vector>

namespace kb::scene {
namespace {

inline constexpr std::size_t kTransformBatchGrainSize = 128U;
inline constexpr std::size_t kFrontierTopGrainSize = 4096U;
inline constexpr std::size_t kSparseTransformFlushLookupFactor = 8U;
inline constexpr std::size_t kDirtyListTransformFlushFactor = 2U;

struct RootSyncProfileTimings {
    std::uint64_t topologyNanoseconds = 0U;
    std::uint64_t denseScratchNanoseconds = 0U;
    std::uint64_t queryCreateNanoseconds = 0U;
    std::uint64_t queryRebuildNanoseconds = 0U;
    std::uint64_t dirtyScanNanoseconds = 0U;
};

[[nodiscard]] bool RootSyncProfileEnabled() noexcept {
    static const bool enabled = [] {
#if defined(_WIN32)
        char* value = nullptr;
        std::size_t size = 0U;
        if (_dupenv_s(&value, &size, "KB_SCENE_ROOT_SYNC_PROFILE") != 0) {
            return false;
        }
        const bool result = value != nullptr && value[0] == '1' && value[1] == '\0';
        std::free(value);
        return result;
#else
        const char* value = std::getenv("KB_SCENE_ROOT_SYNC_PROFILE");
        return value != nullptr && value[0] == '1' && value[1] == '\0';
#endif
    }();
    return enabled;
}

template <typename Duration>
[[nodiscard]] std::uint64_t Nanoseconds(Duration duration) noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
}

void PrintRootSyncProfile(
    const SceneState& state,
    const RootSyncProfileTimings* timings,
    std::size_t dirtyRows,
    std::chrono::steady_clock::time_point updateStart) {
    if (timings == nullptr) {
        return;
    }
    std::cout << "root_sync_profile,roots=" << state.hierarchyRoots.size()
              << ",full_builds=" << state.transformTopology.BuildCount()
              << ",dirty_rows=" << dirtyRows
              << ",topology_ns=" << timings->topologyNanoseconds
              << ",dense_scratch_ns=" << timings->denseScratchNanoseconds
              << ",query_create_ns=" << timings->queryCreateNanoseconds
              << ",query_rebuild_ns=" << timings->queryRebuildNanoseconds
              << ",dirty_scan_ns=" << timings->dirtyScanNanoseconds
              << ",cache_build_ns=" << state.lastTransformHierarchyCacheBuildNanoseconds
              << ",entry_build_ns=" << state.lastTransformHierarchyEntryBuildNanoseconds
              << ",kernel_ns=" << state.lastTransformHierarchyKernelApplyNanoseconds
              << ",frontier_append_ns=" << state.lastTransformHierarchyFrontierAppendNanoseconds
              << ",flush_write_ns=" << state.lastTransformHierarchyFlushWriteNanoseconds
              << ",backend_mark_ns=" << state.lastTransformHierarchyBackendMarkNanoseconds
              << ",propagate_ns=" << state.lastTransformHierarchyPropagateNanoseconds
              << ",total_ns=" << Nanoseconds(std::chrono::steady_clock::now() - updateStart) << '\n';
}

[[nodiscard]] std::size_t HierarchyTrackedSlotCount(const SceneState& state) noexcept {
    return std::max(state.hierarchyOrder.size(), state.denseHierarchyOrder.size());
}

struct TransformValueCache {
    std::vector<SceneTransformValueCacheEntry>& dense;
    std::unordered_map<SceneEntity::IdType, SceneTransformValueCacheEntry>& sparse;
    std::uint64_t buildVersion = 0U;
    std::size_t denseLimit = 0U;
    std::size_t liveCount = 0U;

    void Add(SceneEntity entity, const TransformComponent& transform) {
        const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(entity);
        if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex && denseIndex < denseLimit) {
            dense[denseIndex] =
                SceneTransformValueCacheEntry{ .entity = entity, .transform = transform, .cacheVersion = buildVersion, .valid = true, .dirty = false };
            ++liveCount;
            return;
        }

        sparse[entity.Id()] = SceneTransformValueCacheEntry{ .entity = entity, .transform = transform, .cacheVersion = buildVersion, .valid = true, .dirty = false };
        ++liveCount;
    }

    [[nodiscard]] TransformComponent* Find(SceneEntity entity) noexcept {
        const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(entity);
        if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex && denseIndex < denseLimit) {
            SceneTransformValueCacheEntry& entry = dense[denseIndex];
            if (entry.valid && entry.cacheVersion == buildVersion && entry.entity == entity) {
                return &entry.transform;
            }
        }

        auto transform = sparse.find(entity.Id());
        return transform == sparse.end() ? nullptr : &transform->second.transform;
    }

    [[nodiscard]] const TransformComponent* Find(SceneEntity entity) const noexcept {
        const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(entity);
        if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex && denseIndex < denseLimit) {
            const SceneTransformValueCacheEntry& entry = dense[denseIndex];
            if (entry.valid && entry.cacheVersion == buildVersion && entry.entity == entity) {
                return &entry.transform;
            }
        }

        const auto transform = sparse.find(entity.Id());
        return transform == sparse.end() ? nullptr : &transform->second.transform;
    }

    void MarkDirty(SceneEntity entity) noexcept {
        const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(entity);
        if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex && denseIndex < denseLimit) {
            SceneTransformValueCacheEntry& entry = dense[denseIndex];
            if (entry.valid && entry.cacheVersion == buildVersion && entry.entity == entity) {
                entry.dirty = true;
                return;
            }
        }

        auto transform = sparse.find(entity.Id());
        if (transform != sparse.end()) {
            transform->second.dirty = true;
        }
    }

    [[nodiscard]] const TransformComponent* FindDirty(SceneEntity entity) const noexcept {
        const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(entity);
        if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex && denseIndex < denseLimit) {
            const SceneTransformValueCacheEntry& entry = dense[denseIndex];
            if (entry.valid && entry.cacheVersion == buildVersion && entry.dirty && entry.entity == entity) {
                return &entry.transform;
            }
        }

        const auto transform = sparse.find(entity.Id());
        return transform != sparse.end() && transform->second.dirty ? &transform->second.transform : nullptr;
    }

    [[nodiscard]] std::size_t TrackedCount() const noexcept {
        return liveCount;
    }
};

struct TransformFlushStats {
    std::size_t sparseFlushCount = 0U;
    std::size_t dirtyListFlushCount = 0U;
    std::size_t dirtyListFlushEntityCount = 0U;
    std::size_t batchFlushCount = 0U;
    std::size_t flushedEntityCount = 0U;
    std::size_t parallelFlushCount = 0U;
    std::size_t parallelFlushChunkCount = 0U;
    std::size_t parallelFlushEntityCount = 0U;
    std::size_t parallelFlushWorkerCount = 1U;
    std::uint64_t writeNanoseconds = 0U;
    std::uint64_t backendMarkNanoseconds = 0U;
};

struct TransformFlushContext {
    const TransformValueCache* transformValues = nullptr;
    std::atomic_size_t flushedEntityCount = 0U;
};

struct TransformCacheBuildContext {
    TransformValueCache* cache = nullptr;
    std::mutex sparseMutex;
};

void EnsureWorkerPool(SceneState& state);

[[nodiscard]] TransformValueCache BeginTransformValueCache(SceneState& state) {
    ++state.transformValueCacheBuildVersion;
    if (state.transformValueCacheBuildVersion == 0U) {
        state.transformValueCacheBuildVersion = 1U;
        for (SceneTransformValueCacheEntry& entry : state.transformValueDenseScratch) {
            entry.valid = false;
            entry.cacheVersion = 0U;
        }
    }

    TransformValueCache cache{
        .dense = state.transformValueDenseScratch,
        .sparse = state.transformValueSparseScratch,
        .buildVersion = state.transformValueCacheBuildVersion,
        .denseLimit = state.denseHierarchyParents.size(),
    };
    cache.sparse.clear();
    if (cache.dense.size() < cache.denseLimit) {
        cache.dense.resize(cache.denseLimit);
    }
    return cache;
}

[[nodiscard]] TransformValueCache BuildTransformValueCache(SceneState& state) {
    TransformValueCache cache = BeginTransformValueCache(state);
    cache.sparse.reserve(state.hierarchyOrder.size());
    kb::ecs::Query<TransformComponent> query = state.world.CreateQuery<TransformComponent>();
    if (!query.IsValid()) {
        return cache;
    }

    kb::ecs::QueryExecutionSettings settings;
    settings.maxBatchSize = kTransformBatchGrainSize * 8U;
    settings.policy = kb::ecs::QueryExecutionPolicy::SingleThread;
    TransformCacheBuildContext context{ .cache = &cache };
    if (HierarchyTrackedSlotCount(state) > settings.maxBatchSize * 32U) {
        EnsureWorkerPool(state);
        settings.policy = kb::ecs::QueryExecutionPolicy::ParallelChunks;
        settings.workerPool = state.transformWorkerPool.get();
    }
    kb::ecs::UnsafeHotReadQuery<TransformComponent> hotQuery;
    if (!hotQuery.Rebuild(query, settings)) {
        return cache;
    }

    auto buildBatch = [](const auto& batch, TransformCacheBuildContext& buildContext) {
        const auto* transforms = batch.template Components<0>();
        TransformValueCache& transformCache = *buildContext.cache;
        for (std::size_t row = 0; row < batch.Count(); ++row) {
            const SceneEntity entity = batch.EntityAt(row);
            const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(entity);
            if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex && denseIndex < transformCache.denseLimit) {
                transformCache.dense[denseIndex] = SceneTransformValueCacheEntry{
                    .entity = entity,
                    .transform = transforms[row],
                    .cacheVersion = transformCache.buildVersion,
                    .valid = true,
                    .dirty = false,
                };
                continue;
            }

            std::lock_guard lock{ buildContext.sparseMutex };
            transformCache.sparse[entity.Id()] = SceneTransformValueCacheEntry{
                .entity = entity,
                .transform = transforms[row],
                .cacheVersion = transformCache.buildVersion,
                .valid = true,
                .dirty = false,
            };
        }
    };
    kb::ecs::UnsafeHotRangeDispatchStats dispatchStats;
    if (settings.workerPool != nullptr && settings.policy == kb::ecs::QueryExecutionPolicy::ParallelChunks) {
        dispatchStats = hotQuery.ForEachRangeParallel(settings.maxBatchSize, *settings.workerPool, settings.workerCountOverride, [&context, &buildBatch](const auto& batch, kb::ecs::WorkerContext) {
            buildBatch(batch, context);
        });
    } else {
        dispatchStats = hotQuery.ForEachRange(settings.maxBatchSize, [&context, &buildBatch](const auto& batch) {
            buildBatch(batch, context);
        });
    }
    cache.liveCount = dispatchStats.entities;
    return cache;
}

[[nodiscard]] TransformFlushStats FlushDirtyTransforms(
    SceneState& state,
    TransformValueCache& transformValues,
    std::span<const SceneEntity> updatedEntities) {
    TransformFlushStats stats;
    if (updatedEntities.empty()) {
        return stats;
    }

    const auto writeStart = std::chrono::steady_clock::now();
    if (updatedEntities.size() * kSparseTransformFlushLookupFactor < transformValues.TrackedCount()) {
        stats.sparseFlushCount = 1U;
        stats.dirtyListFlushCount = 1U;
        state.transformHierarchyFlushComponentsScratch.clear();
        kb::ecs::ReserveGeometric(state.transformHierarchyFlushComponentsScratch, updatedEntities.size());
        for (const SceneEntity entity : updatedEntities) {
            if (const TransformComponent* cached = transformValues.FindDirty(entity); cached != nullptr) {
                state.transformHierarchyFlushComponentsScratch.push_back(*cached);
                ++stats.flushedEntityCount;
            }
        }
        state.world.SetMany<TransformComponent>(updatedEntities, state.transformHierarchyFlushComponentsScratch);
        stats.dirtyListFlushEntityCount = stats.flushedEntityCount;
        stats.writeNanoseconds = Nanoseconds(std::chrono::steady_clock::now() - writeStart);
        return stats;
    }

    if (updatedEntities.size() * kDirtyListTransformFlushFactor < transformValues.TrackedCount()) {
        stats.dirtyListFlushCount = 1U;
        state.transformHierarchyFlushComponentsScratch.clear();
        kb::ecs::ReserveGeometric(state.transformHierarchyFlushComponentsScratch, updatedEntities.size());
        for (const SceneEntity entity : updatedEntities) {
            if (const TransformComponent* cached = transformValues.FindDirty(entity); cached != nullptr) {
                state.transformHierarchyFlushComponentsScratch.push_back(*cached);
                ++stats.flushedEntityCount;
            }
        }
        state.world.SetMany<TransformComponent>(updatedEntities, state.transformHierarchyFlushComponentsScratch);
        stats.dirtyListFlushEntityCount = stats.flushedEntityCount;
        stats.writeNanoseconds = Nanoseconds(std::chrono::steady_clock::now() - writeStart);
        return stats;
    }

    stats.batchFlushCount = 1U;
    kb::ecs::Query<TransformComponent> query = state.world.CreateQuery<TransformComponent>();
    if (query.IsValid()) {
        EnsureWorkerPool(state);
        TransformFlushContext context{ .transformValues = &transformValues };
        kb::ecs::QueryExecutionSettings settings;
        settings.maxBatchSize = kTransformBatchGrainSize;
        settings.policy = kb::ecs::QueryExecutionPolicy::ParallelChunks;
        settings.workerPool = state.transformWorkerPool.get();
        kb::ecs::UnsafeHotQuery<TransformComponent> hotQuery;
        const bool allTrackedDirty = updatedEntities.size() >= transformValues.TrackedCount();
        if (!hotQuery.Rebuild(query, settings)) {
            return stats;
        }
        kb::ecs::UnsafeHotRangeDispatchStats dispatchStats;
        if (allTrackedDirty) {
            dispatchStats = hotQuery.ForEachMutableRangeParallel(settings.maxBatchSize, *settings.workerPool, settings.workerCountOverride, [&context](auto& batch, kb::ecs::WorkerContext) {
                auto* transforms = batch.template Components<0>();
                auto* flushContext = &context;
                const TransformValueCache& transformCache = *flushContext->transformValues;
                std::size_t flushedInBatch = 0U;
                for (std::size_t row = 0; row < batch.Count(); ++row) {
                    if (const TransformComponent* cached = transformCache.Find(batch.EntityAt(row)); cached != nullptr) {
                        transforms[row] = *cached;
                        ++flushedInBatch;
                    }
                }
                flushContext->flushedEntityCount.fetch_add(flushedInBatch, std::memory_order_relaxed);
            });
        } else {
            dispatchStats = hotQuery.ForEachMutableRangeParallel(settings.maxBatchSize, *settings.workerPool, settings.workerCountOverride, [&context](auto& batch, kb::ecs::WorkerContext) {
                auto* transforms = batch.template Components<0>();
                auto* flushContext = &context;
                const TransformValueCache& transformCache = *flushContext->transformValues;
                std::size_t flushedInBatch = 0U;
                for (std::size_t row = 0; row < batch.Count(); ++row) {
                    if (const TransformComponent* cached = transformCache.FindDirty(batch.EntityAt(row)); cached != nullptr) {
                        transforms[row] = *cached;
                        ++flushedInBatch;
            }
                }
                flushContext->flushedEntityCount.fetch_add(flushedInBatch, std::memory_order_relaxed);
            });
        }
        stats.flushedEntityCount = context.flushedEntityCount.load(std::memory_order_relaxed);
        stats.parallelFlushCount = 1U;
        stats.parallelFlushChunkCount = dispatchStats.ranges;
        stats.parallelFlushEntityCount = dispatchStats.entities;
        stats.parallelFlushWorkerCount = state.transformWorkerPool->WorkerCount();
    }

    const auto backendMarkStart = std::chrono::steady_clock::now();
    stats.writeNanoseconds = Nanoseconds(backendMarkStart - writeStart);
    if (!state.world.MirrorsValueWrites(state.components.TransformComponentId())) {
        return stats;
    }
    for (const SceneEntity entity : updatedEntities) {
        state.componentStorage.Transforms().MarkWritten(entity);
    }
    stats.backendMarkNanoseconds = Nanoseconds(std::chrono::steady_clock::now() - backendMarkStart);
    return stats;
}

[[nodiscard]] SceneEntity ParentOf(const SceneState& state, SceneEntity entity) noexcept {
    const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(entity);
    if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex && denseIndex < state.denseHierarchyParents.size()) {
        return state.denseHierarchyParents[denseIndex];
    }
    const auto parent = state.hierarchyParents.find(entity.Id());
    return parent == state.hierarchyParents.end() ? SceneEntity{} : parent->second;
}

[[nodiscard]] TransformComponent ParentTransformOf(
    const SceneState& state,
    const TransformValueCache& transformValues,
    SceneEntity parent,
    const TransformComponent& identity) {
    if (!parent.IsValid()) {
        return identity;
    }
    const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(parent);
    if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex && denseIndex < state.denseTransformWorldScratchValid.size()
        && state.denseTransformWorldScratchValid[denseIndex] == state.denseTransformWorldScratchEpoch) {
        return state.denseTransformWorldScratch[denseIndex];
    }
    const auto transform = state.transformWorldScratch.find(parent.Id());
    if (transform != state.transformWorldScratch.end()) {
        return transform->second;
    }
    const TransformComponent* cached = transformValues.Find(parent);
    return cached == nullptr ? identity : *cached;
}

[[nodiscard]] std::uint64_t ParentWorldVersionOf(const SceneState& state, const TransformValueCache& transformValues, SceneEntity parent) noexcept {
    if (!parent.IsValid()) {
        return 0;
    }
    const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(parent);
    if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex && denseIndex < state.denseTransformWorldScratchValid.size()
        && state.denseTransformWorldScratchValid[denseIndex] == state.denseTransformWorldScratchEpoch) {
        return state.denseTransformWorldScratch[denseIndex].worldVersion;
    }
    const auto transform = state.transformWorldScratch.find(parent.Id());
    if (transform != state.transformWorldScratch.end()) {
        return transform->second.worldVersion;
    }
    const TransformComponent* cached = transformValues.Find(parent);
    return cached == nullptr ? 0 : cached->worldVersion;
}

[[nodiscard]] bool ParentDirtyOf(const SceneState& state, SceneEntity parent) noexcept {
    if (!parent.IsValid()) {
        return false;
    }
    const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(parent);
    if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex && denseIndex < state.denseTransformDirtyScratch.size()
        && denseIndex < state.denseTransformWorldScratchValid.size() && state.denseTransformWorldScratchValid[denseIndex] == state.denseTransformWorldScratchEpoch) {
        return state.denseTransformDirtyScratch[denseIndex] != 0U;
    }
    const auto dirty = state.transformDirtyScratch.find(parent.Id());
    return dirty != state.transformDirtyScratch.end() && dirty->second;
}

[[nodiscard]] std::span<const SceneEntity> ChildrenOf(const SceneState& state, SceneEntity entity) noexcept {
    const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(entity);
    if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex && denseIndex < state.denseHierarchyChildren.size()) {
        return state.denseHierarchyChildren[denseIndex];
    }
    const auto children = state.hierarchyChildren.find(entity.Id());
    return children == state.hierarchyChildren.end() ? std::span<const SceneEntity>{} : std::span<const SceneEntity>{ children->second };
}

void StoreTransformScratch(SceneState& state, SceneEntity entity, const TransformComponent& transform, bool dirty) {
    const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(entity);
    if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex) {
        const std::size_t requiredSize = static_cast<std::size_t>(denseIndex) + 1U;
        if (state.denseTransformWorldScratch.size() < requiredSize) {
            state.denseTransformWorldScratch.resize(requiredSize);
            state.denseTransformWorldScratchValid.resize(requiredSize, 0U);
            state.denseTransformDirtyScratch.resize(requiredSize, 0U);
        }
        state.denseTransformWorldScratch[denseIndex] = transform;
        state.denseTransformWorldScratchValid[denseIndex] = state.denseTransformWorldScratchEpoch;
        state.denseTransformDirtyScratch[denseIndex] = dirty ? 1U : 0U;
        return;
    }

    state.transformWorldScratch[entity.Id()] = transform;
    state.transformDirtyScratch[entity.Id()] = dirty;
}

void EnsureWorkerPool(SceneState& state) {
    const auto available = std::max(1U, std::thread::hardware_concurrency());
    const auto limit = state.world.Config().workerThreadLimit;
    const kb::ecs::WorkerPoolConfig config{
        .workerCount = std::min<std::size_t>(available > 1U ? available - 1U : 1U, limit == 0U ? 16U : limit),
    };
    if (state.transformWorkerPool == nullptr) {
        state.transformWorkerPool = std::make_unique<kb::ecs::WorkerPool>(config);
    } else if (!state.transformWorkerPool->Running()) {
        state.transformWorkerPool->Start(config);
    }
}

// The render-proxy lists are derived from these bits when they are read; recording an update (and whether its world
// affine is a translation, for the hot-path report) is all the sync does.
void RecordUpdatedTransform(SceneState& state, SceneEntity entity, const TransformComponent* transform) {
    const bool identityAffine = transform != nullptr && SceneTransformRootHotKernel::CanWriteIdentityAffineFastPath(*transform);
    const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(entity);
    if (denseIndex == kb::ecs::kInvalidGeneratedEntityIndex || denseIndex / 64U >= state.transformUpdatedBits.size()) {
        state.transformUpdatedSparseEntities.push_back(entity);
        state.lastTransformRenderProxyIdentityAffineFastPathCount += identityAffine ? 1U : 0U;
        return;
    }
    std::uint64_t& word = state.transformUpdatedBits[denseIndex / 64U];
    const std::uint64_t bit = std::uint64_t{ 1U } << (denseIndex % 64U);
    state.lastTransformRenderProxyIdentityAffineFastPathCount += identityAffine && (word & bit) == 0U ? 1U : 0U;
    word |= bit;
}

[[nodiscard]] bool IsTransformUpdated(const SceneState& state, SceneEntity entity) noexcept {
    const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(entity);
    if (denseIndex == kb::ecs::kInvalidGeneratedEntityIndex || denseIndex / 64U >= state.transformUpdatedBits.size()) {
        return std::ranges::find(state.transformUpdatedSparseEntities, entity) != state.transformUpdatedSparseEntities.end();
    }
    return (state.transformUpdatedBits[denseIndex / 64U] >> (denseIndex % 64U) & 1U) != 0U;
}


void AppendRenderProxyListEntry(SceneState& state, SceneEntity entity, const TransformComponent& transform) {
    state.transformRenderProxyUpdateEntities.push_back(entity);
    state.transformRenderProxyWorldAffine3x4.push_back(BuildWorldAffine3x4(transform));
}

// Cameras and lights composed this frame are queued for the renderer's proxy updates, as the render-proxy lists
// did when they were built by the sync.
void PublishRenderProxyTransformUpdates(SceneState& state) {
    state.transformRenderProxyListsStale = true;
    SceneComponentIteration::ForEachCamera(state.world, state.components.TransformComponentId(), state.components.CameraComponentId(),
        state.ComponentIterationQueries(), [](SceneEntity entity, const TransformComponent&, const CameraComponent&, void* context) {
            SceneState& state = *static_cast<SceneState*>(context);
            if (IsTransformUpdated(state, entity)) MarkSceneRenderProxyDirty(state, entity);
        }, &state);
    SceneComponentIteration::ForEachLight(state.world, state.components.TransformComponentId(), state.components.LightComponentId(),
        state.ComponentIterationQueries(), [](SceneEntity entity, const TransformComponent&, const LightComponent&, void* context) {
            SceneState& state = *static_cast<SceneState*>(context);
            if (IsTransformUpdated(state, entity)) MarkSceneRenderProxyDirty(state, entity);
        }, &state);
}

void ResetPropagationCursor(SceneState& state) noexcept {
    state.transformPropagationCursorVersion = state.hierarchyTopologyVersion;
    state.transformPropagationCursorLevel = 0U;
    state.transformPropagationCursorOffset = 0U;
}

void PrewarmTransformScratchForCompletedLevels(SceneState& state, const TransformValueCache& transformValues) {
    const std::size_t levelCount = std::min(state.transformPropagationCursorLevel, state.transformTopology.Levels().size());
    for (std::size_t levelIndex = 0; levelIndex < levelCount; ++levelIndex) {
        for (const SceneEntity entity : state.transformTopology.Levels()[levelIndex]) {
            const TransformComponent* transform = transformValues.Find(entity);
            if (transform == nullptr) {
                continue;
            }
            StoreTransformScratch(state, entity, *transform, false);
        }
    }
}

[[nodiscard]] bool ShouldEvaluateTransform(const TransformComponent& transform, bool parentDirty, std::uint64_t parentWorldVersion) noexcept {
    return parentDirty || transform.worldDirty || transform.parentVersion != parentWorldVersion;
}

void PrepareDenseTransformScratch(SceneState& state) {
    const std::size_t requiredSize = state.denseHierarchyParents.size();
    state.denseTransformWorldScratch.resize(requiredSize);
    state.denseTransformWorldScratchValid.resize(requiredSize, 0U);
    state.denseTransformDirtyScratch.resize(requiredSize, 0U);

    if (state.denseTransformWorldScratchEpoch == std::numeric_limits<std::uint32_t>::max()) {
        std::ranges::fill(state.denseTransformWorldScratchValid, 0U);
        state.denseTransformWorldScratchEpoch = 1U;
        return;
    }
    ++state.denseTransformWorldScratchEpoch;
}

[[nodiscard]] bool HasDirtyAncestorInFrontier(const SceneState& state, SceneEntity entity) noexcept {
    std::size_t guard = HierarchyTrackedSlotCount(state) + state.transformDirtyFrontierEntities.size() + 1U;
    SceneEntity parent = ParentOf(state, entity);
    while (parent.IsValid() && guard-- > 0U) {
        if (IsSceneTransformDirtyFrontierMarked(state, parent)) {
            return true;
        }
        parent = ParentOf(state, parent);
    }
    return false;
}

void AppendTransformEntryIfDirty(
    SceneState& state,
    TransformValueCache& transformValues,
    const TransformComponent& identity,
    SceneEntity entity,
    std::vector<SceneTransformBatchEntry>& entries) {
    TransformComponent* transform = transformValues.Find(entity);
    if (transform == nullptr) {
        return;
    }

    const SceneEntity parent = ParentOf(state, entity);
    const bool parentDirty = ParentDirtyOf(state, parent);
    const std::uint64_t parentWorldVersion = parentDirty ? 0U : ParentWorldVersionOf(state, transformValues, parent);
    if (!ShouldEvaluateTransform(*transform, parentDirty, parentWorldVersion)) {
        return;
    }

    entries.push_back(SceneTransformBatchEntry{
        .entity = entity,
        .transform = transform,
        .parentTransform = ParentTransformOf(state, transformValues, parent, identity),
        .state = &state,
        .parentEntity = parent,
        .hasParent = parent.IsValid(),
        .parentDirty = parentDirty,
        .parentWorldVersion = parentWorldVersion,
    });
}

[[nodiscard]] bool CanUseParallelDenseApply(
    const SceneState& state,
    const TransformValueCache& transformValues,
    std::span<const SceneTransformBatchEntry> entries) noexcept {
    if (entries.empty() || !transformValues.sparse.empty() || !state.transformWorldScratch.empty() || !state.transformDirtyScratch.empty()) {
        return false;
    }
    return state.denseTransformWorldScratch.size() >= transformValues.denseLimit
        && state.denseTransformWorldScratchValid.size() >= transformValues.denseLimit
        && state.denseTransformDirtyScratch.size() >= transformValues.denseLimit;
}

void ApplyTransformEntries(
    SceneState& state,
    TransformValueCache& transformValues,
    std::vector<SceneTransformBatchEntry>& entries,
    std::vector<SceneEntity>& updatedEntities) {
    const auto applyStart = std::chrono::steady_clock::now();
    kb::ecs::WorkerPool* workerPool = nullptr;
    if (entries.size() > kTransformBatchGrainSize) {
        EnsureWorkerPool(state);
        workerPool = state.transformWorkerPool.get();
        ++state.lastTransformHierarchyParallelBatchCount;
        state.lastTransformHierarchyParallelEntityCount += entries.size();
        state.lastTransformHierarchyParallelChunkCount += (entries.size() + kTransformBatchGrainSize - 1U) / kTransformBatchGrainSize;
        state.lastTransformHierarchyWorkerCount = std::max(state.lastTransformHierarchyWorkerCount, workerPool->WorkerCount());
    }
    SceneTransformBranchUpdater{}.UpdateBatch(workerPool, entries, kTransformBatchGrainSize);
    state.lastTransformHierarchyInspectedCount += entries.size();

    const bool canUseParallelDenseApply = workerPool != nullptr && CanUseParallelDenseApply(state, transformValues, entries);
    if (canUseParallelDenseApply) {
        const std::size_t chunkCount = (entries.size() + kTransformBatchGrainSize - 1U) / kTransformBatchGrainSize;
        std::vector<SceneTransformApplyChunkStats>& chunkStats = state.transformHierarchyApplyChunkStatsScratch;
        chunkStats.clear();
        chunkStats.resize(chunkCount);
        workerPool->ParallelForChunks(entries.size(), kTransformBatchGrainSize, [&state, &transformValues, &entries, &chunkStats](kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk& chunk) {
            SceneTransformApplyChunkStats localStats;
            for (std::size_t offset = 0; offset < chunk.count; ++offset) {
                const SceneTransformBatchEntry& entry = entries[chunk.begin + offset];
                const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(entry.entity);
                state.denseTransformWorldScratch[denseIndex] = *entry.transform;
                state.denseTransformWorldScratchValid[denseIndex] = state.denseTransformWorldScratchEpoch;
                state.denseTransformDirtyScratch[denseIndex] = entry.updated ? 1U : 0U;
                localStats.rootFastPath += entry.rootFastPath ? 1U : 0U;
                localStats.translatedParentFastPath += entry.translatedParentFastPath ? 1U : 0U;
                localStats.unrotatedParentFastPath += entry.unrotatedParentFastPath ? 1U : 0U;
                localStats.unitScaleParentFastPath += entry.unitScaleParentFastPath ? 1U : 0U;
                localStats.uniformScaleParentFastPath += entry.uniformScaleParentFastPath ? 1U : 0U;
                localStats.staticLocalRotationFastPath += entry.staticLocalRotationFastPath ? 1U : 0U;
                if (entry.updated) {
                    ++localStats.updated;
                    transformValues.dense[denseIndex].dirty = true;
                }
            }
            chunkStats[chunk.index] = localStats;
        });

        std::size_t updatedCount = 0U;
        for (const SceneTransformApplyChunkStats& stats : chunkStats) {
            updatedCount += stats.updated;
            state.lastTransformHierarchyUpdatedCount += stats.updated;
            state.lastTransformHierarchyRootFastPathCount += stats.rootFastPath;
            state.lastTransformHierarchyTranslatedParentFastPathCount += stats.translatedParentFastPath;
            state.lastTransformHierarchyUnrotatedParentFastPathCount += stats.unrotatedParentFastPath;
            state.lastTransformHierarchyUnitScaleParentFastPathCount += stats.unitScaleParentFastPath;
            state.lastTransformHierarchyUniformScaleParentFastPathCount += stats.uniformScaleParentFastPath;
            state.lastTransformHierarchyStaticLocalRotationFastPathCount += stats.staticLocalRotationFastPath;
        }

        if (updatedCount == entries.size()) {
            const std::size_t writeBegin = updatedEntities.size();
            updatedEntities.resize(writeBegin + entries.size());
            for (std::size_t index = 0; index < entries.size(); ++index) {
                updatedEntities[writeBegin + index] = entries[index].entity;
            }
        } else {
            for (const SceneTransformBatchEntry& entry : entries) {
                if (!entry.updated) {
                    continue;
                }
                updatedEntities.push_back(entry.entity);
            }
        }
        state.lastTransformHierarchyKernelApplyNanoseconds += Nanoseconds(std::chrono::steady_clock::now() - applyStart);
        return;
    }

    for (const SceneTransformBatchEntry& entry : entries) {
        StoreTransformScratch(state, entry.entity, *entry.transform, entry.updated);
        if (entry.rootFastPath) {
            ++state.lastTransformHierarchyRootFastPathCount;
        }
        if (entry.translatedParentFastPath) {
            ++state.lastTransformHierarchyTranslatedParentFastPathCount;
        }
        if (entry.unrotatedParentFastPath) {
            ++state.lastTransformHierarchyUnrotatedParentFastPathCount;
        }
        if (entry.unitScaleParentFastPath) {
            ++state.lastTransformHierarchyUnitScaleParentFastPathCount;
        }
        if (entry.uniformScaleParentFastPath) {
            ++state.lastTransformHierarchyUniformScaleParentFastPathCount;
        }
        if (entry.staticLocalRotationFastPath) {
            ++state.lastTransformHierarchyStaticLocalRotationFastPathCount;
        }
        if (entry.updated) {
            ++state.lastTransformHierarchyUpdatedCount;
            transformValues.MarkDirty(entry.entity);
            updatedEntities.push_back(entry.entity);
        }
    }
    state.lastTransformHierarchyKernelApplyNanoseconds += Nanoseconds(std::chrono::steady_clock::now() - applyStart);
}

// Composes the rows of the dirty frontier where they are stored, level by level: first the frontier entities that no other
// one is below, then the children of every row composed (a composed parent changes their world transform too). Only the
// rows of the frontier and of what lies below it are read; each level runs on the worker pool, and the children of a level
// are laid out in order from the counts of its chunks.
void RunHierarchyDirtyFrontier(SceneState& state, bool listUpdatedRows) {
    auto& storage = const_cast<kb::ecs::NativeArchetypeStorage&>(state.world.NativeStorage());
    const kb::ecs::ComponentId transformId = state.components.TransformComponentId();
    const TransformComponent identity = TransformMath::Identity();
    auto& current = state.transformFrontierItemsScratch;
    auto& next = state.transformFrontierNextItemsScratch;
    const auto entryBuildStart = std::chrono::steady_clock::now();
    current.clear();
    next.clear();
    {
        const std::span<const SceneEntity> frontier = state.transformDirtyFrontierEntities;
        std::vector<std::uint8_t>& isTop = state.transformFrontierTopFlagsScratch;
        isTop.resize(frontier.size());
        const std::size_t chunkCount = (frontier.size() + kFrontierTopGrainSize - 1U) / kFrontierTopGrainSize;
        std::vector<std::size_t>& tops = state.transformFrontierChunkChildrenScratch;
        tops.assign(std::max<std::size_t>(chunkCount, 1U), 0U);
        kb::ecs::WorkerPool* workerPool = nullptr;
        if (frontier.size() > kFrontierTopGrainSize) {
            EnsureWorkerPool(state);
            workerPool = state.transformWorkerPool.get();
        }
        const auto forEachTopChunk = [&](auto&& body) {
            if (workerPool == nullptr) {
                body(kb::ecs::WorkerContext{}, kb::ecs::WorkerPoolChunk{ .index = 0U, .begin = 0U, .count = frontier.size() });
            } else {
                workerPool->ParallelForChunks(frontier.size(), kFrontierTopGrainSize, body);
            }
        };
        // a frontier entity is a top unless one above it is queued too: that one composes it with its own subtree
        forEachTopChunk([&](kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk& chunk) {
            std::size_t count = 0U;
            for (std::size_t index = chunk.begin; index < chunk.begin + chunk.count; ++index) {
                isTop[index] = HasDirtyAncestorInFrontier(state, frontier[index]) ? 0U : 1U;
                count += isTop[index];
            }
            tops[workerPool == nullptr ? 0U : chunk.index] = count;
        });
        std::size_t topCount = 0U;
        for (std::size_t& count : tops) {
            const std::size_t chunkTops = count;
            count = topCount;
            topCount += chunkTops;
        }
        current.resize(topCount);
        forEachTopChunk([&](kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk& chunk) {
            std::size_t write = tops[workerPool == nullptr ? 0U : chunk.index];
            for (std::size_t index = chunk.begin; index < chunk.begin + chunk.count; ++index) {
                if (isTop[index] != 0U) current[write++] = SceneTransformFrontierItem{ .entity = frontier[index], .parent = ParentOf(state, frontier[index]) };
            }
        });
    }
    state.lastTransformHierarchyEntryBuildNanoseconds += Nanoseconds(std::chrono::steady_clock::now() - entryBuildStart);

    std::mutex sparseMutex;
    std::vector<std::size_t>& noted = state.transformFrontierArchetypesScratch;
    noted.clear();
    for (bool firstLevel = true; !current.empty(); firstLevel = false) {
        const std::size_t count = current.size();
        kb::ecs::WorkerPool* workerPool = nullptr;
        std::size_t grain = count;
        if (count > kTransformBatchGrainSize) {
            EnsureWorkerPool(state);
            workerPool = state.transformWorkerPool.get();
            grain = std::max(kTransformBatchGrainSize, count / (workerPool->WorkerCount() * 4U));
            ++state.lastTransformHierarchyParallelBatchCount;
            state.lastTransformHierarchyParallelEntityCount += count;
            state.lastTransformHierarchyParallelChunkCount += (count + grain - 1U) / grain;
            state.lastTransformHierarchyWorkerCount = std::max(state.lastTransformHierarchyWorkerCount, workerPool->WorkerCount());
        }
        const std::size_t chunkCount = (count + grain - 1U) / grain;
        std::vector<SceneTransformApplyChunkStats>& chunkStats = state.transformHierarchyApplyChunkStatsScratch;
        chunkStats.assign(chunkCount, SceneTransformApplyChunkStats{});
        std::vector<std::size_t>& chunkChildren = state.transformFrontierChunkChildrenScratch;
        chunkChildren.assign(chunkCount, 0U);
        const auto forEachChunk = [&](auto&& body) {
            if (workerPool == nullptr) {
                body(kb::ecs::WorkerContext{}, kb::ecs::WorkerPoolChunk{ .index = 0U, .begin = 0U, .count = count });
            } else {
                workerPool->ParallelForChunks(count, grain, body);
            }
        };

        forEachChunk([&, firstLevel](kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk& chunk) {
            SceneTransformApplyChunkStats& stats = chunkStats[chunk.index];
            UpdatedTransformBitWriter updatedBits{ state, sparseMutex };
            std::size_t children = 0U;
            for (std::size_t index = chunk.begin; index < chunk.begin + chunk.count; ++index) {
                SceneTransformFrontierItem& item = current[index];
                kb::ecs::NativeComponentRows location;
                TransformComponent* row = static_cast<TransformComponent*>(storage.TryGetMutableComponentRow(item.entity, transformId, location));
                if (row == nullptr) {
                    item.row = nullptr;
                    continue;
                }
                const TransformComponent* parentRow = item.parentRow;
                if (firstLevel) {
                    // Its ancestors are clean, so the row is composed when it is flagged or its parent's world moved.
                    parentRow = item.parent.IsValid() ? static_cast<const TransformComponent*>(storage.TryGetComponentData(item.parent, transformId)) : nullptr;
                    if (!ShouldEvaluateTransform(*row, false, parentRow == nullptr ? 0U : parentRow->worldVersion)) {
                        item.row = nullptr;
                        continue;
                    }
                }
                SceneTransformBatchEntry entry{
                    .entity = item.entity,
                    .transform = row,
                    .parentTransform = parentRow == nullptr ? identity : *parentRow,
                    .state = &state,
                    .parentEntity = item.parent,
                    .hasParent = item.parent.IsValid(),
                    .parentDirty = !firstLevel,
                    .parentWorldVersion = firstLevel && parentRow != nullptr ? parentRow->worldVersion : 0U,
                };
                UpdateSceneTransformBatchEntry(entry);
                item.row = row;
                item.archetype = location.archetypeIndex;
                const auto knownArchetypes = stats.archetypes.begin() + static_cast<std::ptrdiff_t>(stats.archetypeCount);
                if (std::find(stats.archetypes.begin(), knownArchetypes, location.archetypeIndex) == knownArchetypes) {
                    if (stats.archetypeCount < stats.archetypes.size()) stats.archetypes[stats.archetypeCount++] = location.archetypeIndex;
                    else stats.archetypesOverflow = true;
                }
                updatedBits.Record(item.entity, *row);
                children += entry.updated ? ChildrenOf(state, item.entity).size() : 0U;
                stats.updated += entry.updated ? 1U : 0U;
                stats.rootFastPath += entry.rootFastPath ? 1U : 0U;
                stats.translatedParentFastPath += entry.translatedParentFastPath ? 1U : 0U;
                stats.unrotatedParentFastPath += entry.unrotatedParentFastPath ? 1U : 0U;
                stats.unitScaleParentFastPath += entry.unitScaleParentFastPath ? 1U : 0U;
                stats.uniformScaleParentFastPath += entry.uniformScaleParentFastPath ? 1U : 0U;
                stats.staticLocalRotationFastPath += entry.staticLocalRotationFastPath ? 1U : 0U;
            }
            chunkChildren[chunk.index] = children;
        });

        std::size_t levelUpdated = 0U;
        std::size_t childTotal = 0U;
        for (std::size_t chunk = 0U; chunk < chunkCount; ++chunk) {
            const SceneTransformApplyChunkStats& stats = chunkStats[chunk];
            levelUpdated += stats.updated;
            state.lastTransformHierarchyRootFastPathCount += stats.rootFastPath;
            state.lastTransformHierarchyTranslatedParentFastPathCount += stats.translatedParentFastPath;
            state.lastTransformHierarchyUnrotatedParentFastPathCount += stats.unrotatedParentFastPath;
            state.lastTransformHierarchyUnitScaleParentFastPathCount += stats.unitScaleParentFastPath;
            state.lastTransformHierarchyUniformScaleParentFastPathCount += stats.uniformScaleParentFastPath;
            state.lastTransformHierarchyStaticLocalRotationFastPathCount += stats.staticLocalRotationFastPath;
            const std::size_t children = chunkChildren[chunk];
            chunkChildren[chunk] = childTotal;
            childTotal += children;
        }
        state.lastTransformHierarchyInspectedCount += levelUpdated;
        state.lastTransformHierarchyUpdatedCount += levelUpdated;
        state.lastTransformHierarchyDirtyFrontierCount += levelUpdated;

        // A composed row was not flagged for the storage, which keeps a version per table that readers of the transform
        // component compare: note it once for each table that had rows composed.
        const auto noteArchetype = [&](std::size_t archetype) {
            if (std::ranges::find(noted, archetype) != noted.end()) return;
            noted.push_back(archetype);
            storage.NoteComponentWritten(archetype, transformId);
        };
        for (std::size_t chunk = 0U; chunk < chunkCount; ++chunk) {
            const SceneTransformApplyChunkStats& stats = chunkStats[chunk];
            for (std::size_t index = 0U; index < stats.archetypeCount; ++index) noteArchetype(stats.archetypes[index]);
            if (!stats.archetypesOverflow) continue;
            for (std::size_t index = chunk * grain; index < std::min(count, (chunk + 1U) * grain); ++index) {
                if (current[index].row != nullptr) noteArchetype(current[index].archetype);
            }
        }
        if (listUpdatedRows) {
            for (const SceneTransformFrontierItem& item : current) {
                if (item.row == nullptr) continue;
                state.transformHierarchyUpdatedEntitiesScratch.push_back(item.entity);
                state.transformHierarchyUpdatedTransformsScratch.push_back(*item.row);
            }
        }

        const auto frontierAppendStart = std::chrono::steady_clock::now();
        next.resize(childTotal);
        if (childTotal != 0U) {
            forEachChunk([&](kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk& chunk) {
                std::size_t write = chunkChildren[chunk.index];
                for (std::size_t index = chunk.begin; index < chunk.begin + chunk.count; ++index) {
                    const SceneTransformFrontierItem& item = current[index];
                    if (item.row == nullptr) continue;
                    for (const SceneEntity child : ChildrenOf(state, item.entity)) {
                        next[write++] = SceneTransformFrontierItem{ .entity = child, .parent = item.entity, .parentRow = item.row };
                    }
                }
            });
        }
        state.lastTransformHierarchyFrontierAppendNanoseconds += Nanoseconds(std::chrono::steady_clock::now() - frontierAppendStart);
        current.swap(next);
    }
}

[[nodiscard]] bool CanUseNativeRootOnlyDirtyRanges(const SceneState& state) noexcept {
    return state.transformPropagationBudget.maxInspectedEntitiesPerSync == 0U
        && state.transformPropagationCursorLevel == 0U
        && state.transformPropagationCursorOffset == 0U
        && state.transformTopology.Levels().size() == 1U
        && !state.transformTopology.Levels().front().empty();
}

[[nodiscard]] bool RunNativeDirtyRanges(
    SceneState& state,
    std::chrono::steady_clock::time_point updateStart,
    RootSyncProfileTimings* profileTimings,
    bool topologyChanged) {
    using Clock = std::chrono::steady_clock;
    const bool rootsOnly = CanUseNativeRootOnlyDirtyRanges(state);
    if (!rootsOnly && (topologyChanged || state.transformPropagationBudget.maxInspectedEntitiesPerSync != 0U ||
        state.transformPropagationCursorLevel != 0U || state.transformPropagationCursorOffset != 0U)) {
        return false;
    }

    const auto queryCreateStart = profileTimings != nullptr ? Clock::now() : Clock::time_point{};
    if (state.transformRootQueryCache == nullptr) {
        state.transformRootQueryCache = std::make_unique<SceneTransformRootQueryCache>();
    }
    SceneTransformRootQueryCache& queryCache = *state.transformRootQueryCache;
    if (!queryCache.query.IsValid()) {
        queryCache.query = state.world.CreateQuery<TransformComponent>();
    }
    if (profileTimings != nullptr) {
        profileTimings->queryCreateNanoseconds = Nanoseconds(Clock::now() - queryCreateStart);
    }
    if (!queryCache.query.IsValid()) {
        ClearSceneTransformDirtyFrontier(state);
        return false;
    }

    kb::ecs::UnsafeHotQuery<TransformComponent>& hotQuery = queryCache.hotQuery;
    const auto removalSafetyEpoch = state.transformTopology.RemovalSafetyEpoch(state.world.NativeStorage().RemovalVersion());
    const bool unknownRemoval = queryCache.removalSafetyEpoch != removalSafetyEpoch;
    if (unknownRemoval || queryCache.hierarchyTopologyVersion != state.hierarchyTopologyVersion || hotQuery.IsStale(queryCache.query)) {
        if (!rootsOnly && unknownRemoval) {
            // Structural changes can invalidate parents without leaving a live dirty row.
            ClearSceneTransformDirtyFrontier(state);
        }
        const auto queryRebuildStart = profileTimings != nullptr ? Clock::now() : Clock::time_point{};
        const bool rebuilt = hotQuery.IsValid()
            ? hotQuery.Refresh(queryCache.query)
            : hotQuery.Rebuild(queryCache.query, kb::ecs::QueryExecutionSettings{ .maxBatchSize = kTransformBatchGrainSize });
        if (!rebuilt) {
            ClearSceneTransformDirtyFrontier(state);
            return false;
        }
        queryCache.hierarchyTopologyVersion = state.hierarchyTopologyVersion;
        queryCache.removalSafetyEpoch = removalSafetyEpoch;
        if (!rootsOnly && unknownRemoval) {
            return false;
        }
        if (profileTimings != nullptr) {
            profileTimings->queryRebuildNanoseconds = Nanoseconds(Clock::now() - queryRebuildStart);
        }
    }

    const auto dirtyScanStart = profileTimings != nullptr ? Clock::now() : Clock::time_point{};
    // Component writes can change dirty counts without changing archetype structure; each table keeps its count.
    std::size_t dirtyRows = hotQuery.DirtyRowCount<0>(state.world.NativeStorage());
    if (profileTimings != nullptr) {
        profileTimings->dirtyScanNanoseconds = Nanoseconds(Clock::now() - dirtyScanStart);
    }

    ResetPropagationCursor(state);
    // Rows linked to a parent or children are left to the frontier below; every other dirty row is composed by the
    // root kernel wherever the scene's hierarchies are. A linked row that its writer did not queue is queued here.
    std::vector<SceneEntity> unqueuedLinked;
    if (dirtyRows == 0U) {
        const auto finishedAt = Clock::now();
        state.lastTransformHierarchyUpdateNanoseconds = Nanoseconds(finishedAt - updateStart);
        state.lastTransformHierarchyPropagateNanoseconds = state.lastTransformHierarchyUpdateNanoseconds;
        PrintRootSyncProfile(state, profileTimings, dirtyRows, updateStart);
        return true;
    }

    // The composed rows are listed only for an observer of the transform component; otherwise the pass only writes the
    // rows in place.
    const bool listUpdatedRows = state.world.MirrorsValueWrites(state.components.TransformComponentId());
    if (listUpdatedRows) {
        kb::ecs::ReserveGeometric(state.transformHierarchyUpdatedEntitiesScratch, dirtyRows);
        kb::ecs::ReserveGeometric(state.transformHierarchyUpdatedTransformsScratch, dirtyRows);
    }
    auto& nativeStorage = const_cast<kb::ecs::NativeArchetypeStorage&>(state.world.NativeStorage());
    const auto applyStart = Clock::now();
    if (dirtyRows > kTransformBatchGrainSize * 4U) {
        EnsureWorkerPool(state);
        if (listUpdatedRows) {
            state.transformHierarchyUpdatedEntitiesScratch.resize(dirtyRows);
            state.transformHierarchyUpdatedTransformsScratch.resize(dirtyRows);
        }
        std::atomic_size_t inspectedCount{ 0U };
        std::atomic_size_t updatedCount{ 0U };
        std::atomic_size_t rootFastPathCount{ 0U };
        std::mutex unqueuedMutex;
        std::mutex sparseUpdatedMutex;
        const kb::ecs::UnsafeHotDirtyRangeDispatchStats dispatchStats = hotQuery.ForEachDirtyMutableRangeParallel<0>(
            nativeStorage,
            kTransformBatchGrainSize,
            *state.transformWorkerPool,
            0U,
            true,
            [&state, rootsOnly, listUpdatedRows, &inspectedCount, &updatedCount, &rootFastPathCount, &unqueuedLinked, &unqueuedMutex, &sparseUpdatedMutex](
                kb::ecs::UnsafeHotMutableChunk<TransformComponent>& chunk,
                std::size_t dirtyCount,
                kb::ecs::WorkerContext workerContext) {
                static_cast<void>(workerContext);
                static_cast<void>(dirtyCount);
                std::size_t localUpdatedCount = 0U;
                std::size_t localLinkedCount = 0U;
                std::size_t localRootFastPathCount = 0U;
                TransformComponent* transforms = chunk.template Components<0>();
                for (std::size_t row = 0U; row < chunk.Count(); ++row) {
                    if (transforms[row].worldDirty) {
                        const bool linked = !rootsOnly && SceneHierarchyCache::HasTransformLink(state, chunk.EntityAt(row));
                        localUpdatedCount += linked ? 0U : 1U;
                        localLinkedCount += linked ? 1U : 0U;
                    }
                }
                // next to hierarchies only the rows composed here count; the frontier lanes count the linked ones
                inspectedCount.fetch_add(rootsOnly ? chunk.Count() : localUpdatedCount, std::memory_order_relaxed);
                if (localUpdatedCount + localLinkedCount == 0U) {
                    return;
                }

                const std::size_t writeBegin = updatedCount.fetch_add(localUpdatedCount, std::memory_order_relaxed);
                std::size_t writeOffset = 0U;
                UpdatedTransformBitWriter updatedBits{ state, sparseUpdatedMutex };
                for (std::size_t row = 0U; row < chunk.Count(); ++row) {
                    TransformComponent& transform = transforms[row];
                    if (transform.worldDirty) {
                        if (localLinkedCount != 0U && SceneHierarchyCache::HasTransformLink(state, chunk.EntityAt(row))) {
                            if (!IsSceneTransformDirtyFrontierMarked(state, chunk.EntityAt(row))) {
                                const std::lock_guard lock{ unqueuedMutex };
                                unqueuedLinked.push_back(chunk.EntityAt(row));
                            }
                            continue;
                        }
                        if (SceneTransformRootHotKernel::CanApplyIdentityRotationFastPath(transform)) {
                            SceneTransformRootHotKernel::ApplyIdentityRotationRoot(transform);
                            ++localRootFastPathCount;
                        } else {
                            transform = TransformMath::ComposeRoot(transform);
                        }
                        if (listUpdatedRows) {
                            state.transformHierarchyUpdatedEntitiesScratch[writeBegin + writeOffset] = chunk.EntityAt(row);
                            state.transformHierarchyUpdatedTransformsScratch[writeBegin + writeOffset] = transform;
                            ++writeOffset;
                        }
                        updatedBits.Record(chunk.EntityAt(row), transform);
                    }
                }
                rootFastPathCount.fetch_add(localRootFastPathCount, std::memory_order_relaxed);
            });
        state.lastTransformHierarchyInspectedCount += inspectedCount.load(std::memory_order_relaxed);
        const std::size_t finalUpdatedCount = updatedCount.load(std::memory_order_relaxed);
        if (listUpdatedRows) {
            state.transformHierarchyUpdatedEntitiesScratch.resize(finalUpdatedCount);
            state.transformHierarchyUpdatedTransformsScratch.resize(finalUpdatedCount);
        }
        state.lastTransformHierarchyUpdatedCount += finalUpdatedCount;
        state.lastTransformHierarchyRootFastPathCount += rootFastPathCount.load(std::memory_order_relaxed);
        ++state.lastTransformHierarchyParallelBatchCount;
        state.lastTransformHierarchyParallelChunkCount += dispatchStats.ranges;
        state.lastTransformHierarchyParallelEntityCount += dispatchStats.entities;
        state.lastTransformHierarchyWorkerCount = std::max(state.lastTransformHierarchyWorkerCount, state.transformWorkerPool->WorkerCount());
        // the workers append linked rows in any order
        std::ranges::sort(unqueuedLinked, {}, [](SceneEntity entity) { return entity.Id(); });
    } else {
        static_cast<void>(hotQuery.ForEachDirtyMutableRange<0>(
            nativeStorage,
            kTransformBatchGrainSize,
            state.transformNativeDirtyRangesScratch,
            true,
            [&state, rootsOnly, listUpdatedRows, &unqueuedLinked](kb::ecs::UnsafeHotMutableChunk<TransformComponent>& chunk, std::size_t dirtyCount) {
                state.lastTransformHierarchyInspectedCount += rootsOnly ? chunk.Count() : 0U;
                static_cast<void>(dirtyCount);
                TransformComponent* transforms = chunk.template Components<0>();
                for (std::size_t row = 0U; row < chunk.Count(); ++row) {
                    TransformComponent& transform = transforms[row];
                    if (!transform.worldDirty) {
                        continue;
                    }
                    if (!rootsOnly && SceneHierarchyCache::HasTransformLink(state, chunk.EntityAt(row))) {
                        if (!IsSceneTransformDirtyFrontierMarked(state, chunk.EntityAt(row))) unqueuedLinked.push_back(chunk.EntityAt(row));
                        continue;
                    }
                    if (SceneTransformRootHotKernel::CanApplyIdentityRotationFastPath(transform)) {
                        SceneTransformRootHotKernel::ApplyIdentityRotationRoot(transform);
                        ++state.lastTransformHierarchyRootFastPathCount;
                    } else {
                        transform = TransformMath::ComposeRoot(transform);
                    }
                    ++state.lastTransformHierarchyUpdatedCount;
                    state.lastTransformHierarchyInspectedCount += rootsOnly ? 0U : 1U;
                    if (listUpdatedRows) {
                        state.transformHierarchyUpdatedEntitiesScratch.push_back(chunk.EntityAt(row));
                        state.transformHierarchyUpdatedTransformsScratch.push_back(transform);
                    }
                    RecordUpdatedTransform(state, chunk.EntityAt(row), &transform);
                }
            }));
    }
    if (!rootsOnly) {
        for (const SceneEntity entity : unqueuedLinked) EnqueueSceneTransformDirtyFrontierUnchecked(state, entity);
        RunHierarchyDirtyFrontier(state, listUpdatedRows);
    }
    const auto applyEnd = Clock::now();
    state.lastTransformHierarchyKernelApplyNanoseconds = Nanoseconds(applyEnd - applyStart);
    state.lastTransformHierarchyUpdateNanoseconds = Nanoseconds(applyEnd - updateStart);
    state.lastTransformHierarchyPropagateNanoseconds = state.lastTransformHierarchyUpdateNanoseconds;
    state.lastTransformHierarchyFlushWriteNanoseconds = state.lastTransformHierarchyKernelApplyNanoseconds;
    state.lastTransformHierarchyFlushedEntityCount = state.lastTransformHierarchyUpdatedCount;

    const auto backendMarkStart = Clock::now();
    if (state.world.MirrorsValueWrites(state.components.TransformComponentId())) {
        const std::size_t updatedTransformCount = std::min(
            state.transformHierarchyUpdatedEntitiesScratch.size(),
            state.transformHierarchyUpdatedTransformsScratch.size());
        for (std::size_t index = 0U; index < updatedTransformCount; ++index) {
            const auto entity = state.transformHierarchyUpdatedEntitiesScratch[index];
            const auto* current = static_cast<const TransformComponent*>(nativeStorage.TryGetComponentData(entity, state.components.TransformComponentId()));
            if (current == nullptr) continue;
            // An earlier OnSet may have edited or migrated this row. Publish
            // the current canonical value, with no pointer held across callbacks.
            const TransformComponent published = *current;
            SceneComponentAccess::SetExisting(
                state.world.NativeHandle(),
                entity,
                state.components.TransformComponentId(),
                sizeof(TransformComponent),
                &published);
        }
    }
    state.lastTransformHierarchyBackendMarkNanoseconds = Nanoseconds(Clock::now() - backendMarkStart);
    if (state.lastTransformHierarchyUpdatedCount != 0U) {
        PublishRenderProxyTransformUpdates(state);
    }
    ClearSceneTransformDirtyFrontier(state);
    PrintRootSyncProfile(state, profileTimings, dirtyRows, updateStart);
    return true;
}

} // namespace

void EnsureSceneTransformWorkerPool(SceneState& state) {
    EnsureWorkerPool(state);
}

struct TransformPassRangeWrites {
    struct ExtraRows {
        std::size_t extraIndex;
        std::size_t firstRow;
        std::size_t count;
    };
    std::vector<ExtraRows> extraRows;
    std::uint8_t fullExtraMask = 0U;
    bool transformWritten = false;
};

// Retained outputs hold no writer, mutex reference, or storage pointer.
struct TransformPassOutput {
    struct Deferred {
        SceneEntity entity;
        kb::ecs::NativeComponentRows row;
    };

    void Clear() noexcept {
        deferred.clear();
        written = 0U;
    }

    std::vector<Deferred> deferred;
    std::size_t written = 0U;
};

struct SceneTransformPassArena {
    void Prepare(std::size_t taskCount, std::size_t recordCount) {
        if (outputs.size() < taskCount) outputs.resize(taskCount);
        if (rangeWrites.size() < recordCount) rangeWrites.resize(recordCount);
        // Clear even slots skipped by an exception-stopped worker; no previous pass may publish again.
        for (std::size_t index = 0U; index < taskCount; ++index) outputs[index].Clear();
        for (std::size_t index = 0U; index < recordCount; ++index) {
            rangeWrites[index].extraRows.clear();
            rangeWrites[index].fullExtraMask = 0U;
            rangeWrites[index].transformWritten = false;
        }
        aliasOutput.Clear();
        writes.clear();
        for (auto& entities : observedExtraRows) entities.clear();
    }

    std::vector<TransformPassOutput> outputs;
    std::vector<TransformPassRangeWrites> rangeWrites;
    TransformPassOutput aliasOutput;
    std::vector<kb::ecs::NativeComponentRows> writes;
    std::array<std::vector<kb::ecs::Entity>, TransformRowRange::kMaxExtraComponents> observedExtraRows;
    bool leased = false;
};

class TransformPassArenaLease {
public:
    explicit TransformPassArenaLease(SceneState& state) {
        if (state.transformPassArena == nullptr) state.transformPassArena = std::make_shared<SceneTransformPassArena>();
        // OnSet may start another pass after workers join. Its outputs must not replace those being published.
        arena_ = state.transformPassArena->leased ? std::make_shared<SceneTransformPassArena>() : state.transformPassArena;
        arena_->leased = true;
    }

    TransformPassArenaLease(const TransformPassArenaLease&) = delete;
    TransformPassArenaLease& operator=(const TransformPassArenaLease&) = delete;
    ~TransformPassArenaLease() { arena_->leased = false; }

    [[nodiscard]] SceneTransformPassArena& Arena() const noexcept { return *arena_; }

private:
    std::shared_ptr<SceneTransformPassArena> arena_;
};

// Live worker context: its writer and stack mutex references end before the worker joins.
struct TransformPassTask {
    TransformPassTask(SceneState& state, std::mutex& sparseMutex, TransformPassOutput& output) noexcept
        : state(state), updatedBits(state, sparseMutex), deferred(output.deferred) {}

    SceneState& state;
    UpdatedTransformBitWriter updatedBits;
    std::vector<TransformPassOutput::Deferred>& deferred;
    TransformPassRangeWrites* rangeWrites = nullptr;
    std::size_t written = 0U;
    bool trackPrefab = false;
};

// Writes four complete rows: streaming stores when the rows are 16-byte aligned.
void StoreRows4(TransformComponent* destination, const std::array<TransformComponent, 4U>& rows) noexcept {
    static_assert(sizeof(TransformComponent) % 16U == 0U);
#if defined(_M_X64) || defined(__x86_64__)
    if ((reinterpret_cast<std::uintptr_t>(destination) & 15U) == 0U) {
        const auto* source = reinterpret_cast<const __m128i*>(rows.data());
        auto* target = reinterpret_cast<__m128i*>(destination);
        for (std::size_t index = 0U; index < sizeof(TransformComponent) * 4U / 16U; ++index) _mm_stream_si128(target + index, _mm_loadu_si128(source + index));
        return;
    }
#endif
    std::copy(rows.begin(), rows.end(), destination);
}

class TransformPassAccess {
public:
    static void Bind(TransformRowRange& range, const kb::ecs::MutableQueryTableDispatchRecord& record, std::size_t extraCount,
        TransformPassTask& task, TransformPassRangeWrites& writes, bool composes) noexcept {
        range.entityIds_ = record.entityIds;
        range.rows_ = static_cast<TransformComponent*>(record.fieldComponents[0]);
        range.count_ = record.entityCount;
        for (std::size_t extra = 0U; extra < TransformRowRange::kMaxExtraComponents; ++extra) {
            range.extraColumns_[extra] = extra < extraCount ? record.fieldComponents[extra + 1U] : nullptr;
        }
        range.pass_ = &task;
        task.rangeWrites = &writes;
        range.archetypeIndex_ = record.nativeArchetypeIndex;
        range.chunkIndex_ = record.nativeChunkIndex;
        range.composes_ = composes;
    }

    static void SetLocal(TransformRowRange& range, std::size_t row, const Vec3& position, const Quat& rotation, const Vec3& scale) {
        auto& task = *static_cast<TransformPassTask*>(range.pass_);
        TransformComponent& transform = range.rows_[row];
        const SceneEntity entity{ range.entityIds_[row] };
        std::uint32_t nodeIndex = 0U;
        const bool deferred = !range.composes_ || SceneHierarchyCache::HasTransformLink(task.state, entity) ||
            (task.trackPrefab && task.state.prefabInstances.FindContainingEntity(entity, nodeIndex).IsValid());
        // Allocate the deferred record before modifying the value, so a failed allocation cannot lose a write.
        if (deferred) {
            task.deferred.push_back({ entity, kb::ecs::NativeComponentRows{ .archetypeIndex = range.archetypeIndex_, .chunkIndex = range.chunkIndex_, .firstRow = row, .count = 1U } });
        }
        transform.localPosition = position;
        transform.localRotation = rotation;
        transform.localScale = scale;
        ++transform.localVersion;
        transform.worldDirty = true;
        ++task.written;
        task.rangeWrites->transformWritten = true;
        if (deferred) return;
        ComposeSceneTransformRoot(transform);
        task.updatedBits.Record(entity, transform);
    }

    static void SetLocalPrecise(TransformRowRange& range, std::size_t row, const kb::math::DVec3& translation, const Quat& rotation, const Vec3& scale) {
        Vec3 view{};
        Vec3 residual{};
        SplitTranslation(translation, view, residual);
        auto& task = *static_cast<TransformPassTask*>(range.pass_);
        const SceneEntity entity{ range.entityIds_[row] };
        SceneTransformResiduals& residuals = task.state.transformResiduals;
        // Entries of different rows are independent: workers store them concurrently.
        if (!IsZero(residual) || residuals.Find(entity) != nullptr) residuals.Acquire(entity).local = residual;
        SetLocal(range, row, view, rotation, scale);
    }

    static void SetLocalBatch(TransformRowRange& range, std::size_t firstRow, std::span<const RowLocalTRS> supplied) {
        if (firstRow > range.count_ || supplied.size() > range.count_ - firstRow) {
            throw std::out_of_range("A local TRS batch is outside its transform range");
        }
        if (supplied.empty()) return;
        auto& task = *static_cast<TransformPassTask*>(range.pass_);
        for (std::size_t offset = 0U; offset < supplied.size();) {
            const std::size_t remaining = supplied.size() - offset;
            const std::size_t count = std::min<std::size_t>(4U, remaining);
            // Load complete payloads before any group store; no alignment or consecutive-ID promise.
            std::array<RowLocalTRS, 4U> values{};
            std::array<Quat, 4U> normalized{};
            for (std::size_t lane = 0U; lane < count; ++lane) {
                values[lane] = supplied[offset + lane];
            }
            // Use the existing per-row SetLocal gates, without a retained range certificate.
            // Any active prefab tracking conservatively keeps this whole group scalar.
            bool packed = count == 4U && range.composes_ && !task.trackPrefab;
            if (packed) {
                for (std::size_t lane = 0U; lane < 4U; ++lane) {
                    const SceneEntity entity{ range.entityIds_[firstRow + offset + lane] };
                    const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(entity);
                    // Exactly Record's allocation-free branch, including bounded adopted IDs.
                    if (denseIndex == kb::ecs::kInvalidGeneratedEntityIndex ||
                        denseIndex / 64U >= task.state.transformUpdatedBits.size() ||
                        SceneHierarchyCache::HasTransformLink(task.state, entity)) {
                        packed = false;
                        break;
                    }
                }
            }
            if (packed) packed = exact_local_batch::TryNormalizeRoot4Exact(values, normalized);
            if (!packed) {
                // Entire groups/tails retain ordinary per-row allocation, exception and deferred behavior.
                for (std::size_t lane = 0U; lane < count; ++lane) {
                    const auto& value = values[lane];
                    SetLocal(range, firstRow + offset + lane, value.position, value.rotation, value.scale);
                }
            } else {
                std::array<TransformComponent, 4U> rows{};
                for (std::size_t lane = 0U; lane < 4U; ++lane) {
                    TransformComponent& transform = rows[lane];
                    const auto& value = values[lane];
                    transform.localPosition = value.position;
                    transform.localRotation = value.rotation;
                    transform.localScale = value.scale;
                    // Count each row's versions up as SetLocal does: a shared value could repeat one a reader has seen.
                    const TransformComponent& old = range.rows_[firstRow + offset + lane];
                    transform.localVersion = old.localVersion + 1U;
                    transform.worldPosition = value.position;
                    transform.worldRotation = normalized[lane];
                    transform.worldScale = value.scale;
                    transform.parentVersion = 0U;
                    transform.worldVersion = old.worldVersion + 1U;
                    transform.worldDirty = false;
                    ++task.written;
                    task.rangeWrites->transformWritten = true;
                    // Preserve existing full ID, word grouping and identity-affine accounting in storage order.
                    task.updatedBits.Record(SceneEntity{ range.entityIds_[firstRow + offset + lane] }, transform);
                }
                StoreRows4(&range.rows_[firstRow + offset], rows);
            }
            offset += count;
        }
#if defined(_M_X64) || defined(__x86_64__)
        _mm_sfence();
#endif
    }

    static void DeclareColumnWritable(const TransformRowRange& range, std::size_t extraIndex) noexcept {
        if (extraIndex >= TransformRowRange::kMaxExtraComponents || range.extraColumns_[extraIndex] == nullptr) return;
        auto& task = *static_cast<TransformPassTask*>(range.pass_);
        task.rangeWrites->fullExtraMask |= static_cast<std::uint8_t>(1U << extraIndex);
    }

    static void DeclareColumnRowsWritable(const TransformRowRange& range, std::size_t extraIndex, std::size_t firstRow, std::size_t count) {
        if (extraIndex >= TransformRowRange::kMaxExtraComponents || range.extraColumns_[extraIndex] == nullptr ||
            firstRow > range.count_ || count > range.count_ - firstRow) {
            throw std::out_of_range("An extra-column write span is outside its transform range");
        }
        if (count == 0U) return;
        auto& task = *static_cast<TransformPassTask*>(range.pass_);
        if ((task.rangeWrites->fullExtraMask & (1U << extraIndex)) == 0U) {
            task.rangeWrites->extraRows.push_back({ extraIndex, firstRow, count });
        }
    }
};

void TransformRowRange::SetLocal(std::size_t row, const Vec3& position, const Quat& rotation, const Vec3& scale) {
    TransformPassAccess::SetLocal(*this, row, position, rotation, scale);
}

void TransformRowRange::SetLocal(std::size_t row, const kb::math::DVec3& translation, const Quat& rotation, const Vec3& scale) {
    TransformPassAccess::SetLocalPrecise(*this, row, translation, rotation, scale);
}

void TransformRowRange::SetLocalBatch(std::size_t firstRow, std::span<const RowLocalTRS> values) {
    TransformPassAccess::SetLocalBatch(*this, firstRow, values);
}

void TransformRowRange::DeclareColumnWritable(std::size_t extraIndex) const noexcept {
    TransformPassAccess::DeclareColumnWritable(*this, extraIndex);
}

void TransformRowRange::DeclareColumnRowsWritable(std::size_t extraIndex, std::size_t firstRow, std::size_t count) const {
    TransformPassAccess::DeclareColumnRowsWritable(*this, extraIndex, firstRow, count);
}

TransformPassStats RunSceneTransformPass(SceneState& state, std::size_t grainRows, std::span<const kb::ecs::ComponentId> extraComponents,
    SceneTransforms::TransformRangeBody body, void* context, bool declaredAccess) {
    TransformPassStats stats;
    if (body == nullptr) return stats;
    if (extraComponents.size() > TransformRowRange::kMaxExtraComponents) {
        throw std::invalid_argument("A transform pass takes at most four extra components");
    }
    if (state.transformPassRunning) {
        throw std::logic_error("A transform pass cannot run inside another one");
    }
    // Rows written with a double-precision translation store residuals from the workers.
    if (!state.transformResiduals.Empty()) state.transformResiduals.Reserve(state.denseHierarchyParents.size());
    const kb::ecs::ComponentId transformId = state.components.TransformComponentId();
    auto& storage = const_cast<kb::ecs::NativeArchetypeStorage&>(state.world.NativeStorage());
    std::array<kb::ecs::ComponentId, 5U> ids{ transformId };
    for (std::size_t extra = 0U; extra < extraComponents.size(); ++extra) {
        if (extraComponents[extra] == 0U) return stats;
        ids[extra + 1U] = extraComponents[extra];
    }
    std::array<kb::ecs::ComponentId, 5U> uniqueIds{};
    std::array<std::size_t, 5U> fieldIndices{};
    std::size_t uniqueCount = 0U;
    for (std::size_t field = 0U; field < extraComponents.size() + 1U; ++field) {
        std::size_t unique = 0U;
        while (unique < uniqueCount && uniqueIds[unique] != ids[field]) ++unique;
        if (unique == uniqueCount) uniqueIds[uniqueCount++] = ids[field];
        fieldIndices[field] = unique;
    }
    const bool aliasedColumns = uniqueCount != extraComponents.size() + 1U;
    const std::span<const kb::ecs::ComponentId> queried{ uniqueIds.data(), uniqueCount };
    if (state.transformPassRecordsVersion != storage.StructuralVersion() || state.transformPassRecordIds != ids || state.transformPassRecords.empty()) {
        std::size_t firstChangedRecord = 0U;
        if (aliasedColumns || state.transformPassRecordIds != ids || state.transformPassRecords.empty() ||
            !storage.RefreshMutableQueryRecords(queried, {}, {}, state.transformPassRecordsVersion,
                state.transformPassRecords, firstChangedRecord)) {
            storage.CollectMutableQueryRecords(queried, {}, {}, state.transformPassRecords);
        }
        if (aliasedColumns) {
            // Native queries require distinct component IDs. Expand the unique query's fields in reverse,
            // so each public Extra index still exposes its alias without overwriting a source field early.
            for (auto& record : state.transformPassRecords) {
                for (std::size_t field = extraComponents.size() + 1U; field != 0U; --field) {
                    const std::size_t index = field - 1U;
                    record.fieldComponents[index] = record.fieldComponents[fieldIndices[index]];
                    record.componentVersions[index] = record.componentVersions[fieldIndices[index]];
                    record.componentDirtyCounts[index] = record.componentDirtyCounts[fieldIndices[index]];
                }
            }
        }
        // cameras and lights are queued for the renderer's proxies by the sync
        state.transformPassDeferredArchetypes.clear();
        for (const kb::ecs::ComponentId componentId : { state.components.CameraComponentId(), state.components.LightComponentId() }) {
            if (componentId == 0U) continue;
            for (const kb::ecs::NativeArchetypeMatch& match : storage.MatchingArchetypes(std::span<const kb::ecs::ComponentId>{ &componentId, 1U })) {
                if (state.transformPassDeferredArchetypes.size() <= match.archetypeIndex) state.transformPassDeferredArchetypes.resize(match.archetypeIndex + 1U, false);
                state.transformPassDeferredArchetypes[match.archetypeIndex] = true;
            }
        }
        state.transformPassRecordIds = ids;
        state.transformPassRecordsVersion = storage.StructuralVersion();
    }
    const auto& records = state.transformPassRecords;
    if (records.empty()) return stats;

    PrepareSceneTransformUpdateRecording(state);
    // An observed transform is published in order after the pass, and cameras and lights are queued for the
    // renderer's proxies by the sync: their rows take the sync's lanes.
    const bool observed = state.world.MirrorsValueWrites(transformId);
    const bool trackPrefab = !state.suppressPrefabDirtyTracking && state.prefabInstances.Count() > 0U;

    std::size_t rows = 0U;
    for (const auto& record : records) rows += record.entityCount;
    const std::size_t recordsPerTask = std::max<std::size_t>(1U, grainRows * records.size() / std::max<std::size_t>(rows, 1U));
    const std::size_t taskCount = (records.size() + recordsPerTask - 1U) / recordsPerTask;
    TransformPassArenaLease arenaLease{ state };
    SceneTransformPassArena& arena = arenaLease.Arena();
    arena.Prepare(taskCount, records.size());
    const auto outputs = std::span<TransformPassOutput>{ arena.outputs }.first(taskCount);
    const auto rangeWrites = std::span<TransformPassRangeWrites>{ arena.rangeWrites }.first(records.size());
    std::mutex sparseMutex;
    std::atomic_bool stopped{ false };
    std::exception_ptr firstException;
    std::mutex exceptionMutex;
    EnsureWorkerPool(state);
    state.transformPassRunning = true;
    {
        const auto iterationGuard = state.world.EnterIteration();
        try {
            state.transformWorkerPool->ParallelForChunks(records.size(), recordsPerTask, [&](kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk& chunk) {
                TransformPassTask task{ state, sparseMutex, outputs[chunk.index] };
                task.trackPrefab = trackPrefab;
                for (std::size_t index = chunk.begin; index < chunk.begin + chunk.count && !stopped.load(std::memory_order_relaxed); ++index) {
                    const auto& record = records[index];
                    const bool composes = !observed && (record.nativeArchetypeIndex >= state.transformPassDeferredArchetypes.size() ||
                        !state.transformPassDeferredArchetypes[record.nativeArchetypeIndex]);
                    TransformRowRange range;
                    TransformPassAccess::Bind(range, record, extraComponents.size(), task, rangeWrites[index], composes);
                    if (!declaredAccess) rangeWrites[index].fullExtraMask = static_cast<std::uint8_t>((1U << extraComponents.size()) - 1U);
                    try {
                        body(range, context);
                    } catch (...) {
                        const std::lock_guard lock{ exceptionMutex };
                        if (firstException == nullptr) firstException = std::current_exception();
                        stopped.store(true, std::memory_order_relaxed);
                    }
                }
                task.updatedBits.Finish();
                outputs[chunk.index].written = task.written;
            });
        } catch (...) {
            if (firstException == nullptr) firstException = std::current_exception();
        }
    }
    state.transformPassRunning = false;

    TransformPassTask transformAliasTask{ state, sparseMutex, arena.aliasOutput };
    transformAliasTask.trackPrefab = trackPrefab;
    std::uint8_t transformExtraMask = 0U;
    for (std::size_t extra = 0U; extra < extraComponents.size(); ++extra) {
        if (extraComponents[extra] == transformId) transformExtraMask |= static_cast<std::uint8_t>(1U << extra);
    }
    if (transformExtraMask != 0U) {
        // Direct mutable transform aliases declare a further local write. Reconcile their final local TRS
        // before metadata or observers are published, with the same setter and composition as SetLocal.
        for (std::size_t index = 0U; index < records.size(); ++index) {
            const auto& record = records[index];
            auto& written = rangeWrites[index];
            const bool full = (written.fullExtraMask & transformExtraMask) != 0U;
            const bool composes = !observed && (record.nativeArchetypeIndex >= state.transformPassDeferredArchetypes.size() ||
                !state.transformPassDeferredArchetypes[record.nativeArchetypeIndex]);
            TransformRowRange range;
            TransformPassAccess::Bind(range, record, extraComponents.size(), transformAliasTask, written, composes);
            for (std::size_t row = 0U; row < record.entityCount; ++row) {
                const bool partial = !full && std::any_of(written.extraRows.begin(), written.extraRows.end(), [&](const auto& declared) {
                    return (transformExtraMask & (1U << declared.extraIndex)) != 0U &&
                        row >= declared.firstRow && row - declared.firstRow < declared.count;
                });
                if (!full && !partial) continue;
                const TransformComponent& current = range.Get(row);
                TransformPassAccess::SetLocal(range, row, current.localPosition, current.localRotation, current.localScale);
            }
        }
        transformAliasTask.updatedBits.Finish();
    }

    // Serial publication: the version of each written table once, then the deferred rows as Set flags them.
    for (std::size_t index = 0U; index < records.size(); ++index) {
        stats.rowsVisited += records[index].entityCount;
        ++stats.ranges;
    }
    std::vector<kb::ecs::NativeComponentRows>& deferredRows = state.transformSetManyRowsScratch;
    deferredRows.clear();
    const auto collectDeferred = [&deferredRows, observed](const TransformPassOutput& output) {
        if (observed) return;
        for (const auto& deferred : output.deferred) {
            kb::ecs::NativeComponentRows* run = deferredRows.empty() ? nullptr : &deferredRows.back();
            if (run != nullptr && run->archetypeIndex == deferred.row.archetypeIndex && run->chunkIndex == deferred.row.chunkIndex &&
                run->firstRow + run->count == deferred.row.firstRow) {
                ++run->count;
            } else {
                deferredRows.push_back(deferred.row);
            }
        }
    };
    for (const auto& output : outputs) {
        stats.rowsWritten += output.written;
        stats.rowsDeferred += output.deferred.size();
        collectDeferred(output);
    }
    collectDeferred(arena.aliasOutput);
    std::size_t lastWrittenArchetype = std::numeric_limits<std::size_t>::max();
    for (std::size_t index = 0U; index < records.size(); ++index) {
        const auto& record = records[index];
        if (rangeWrites[index].transformWritten && record.nativeArchetypeIndex != lastWrittenArchetype) {
            lastWrittenArchetype = record.nativeArchetypeIndex;
            storage.NoteComponentWritten(lastWrittenArchetype, transformId);
        }
    }
    if (!deferredRows.empty()) storage.MarkComponentRowsModified(transformId, deferredRows);

    auto& observedExtraRows = arena.observedExtraRows;
    // Publish all metadata before any observer can change structure. Capture entity identities for observer
    // publication now; record pointers/row locations can become invalid during the first OnSet callback.
    for (std::size_t extra = 0U; extra < extraComponents.size(); ++extra) {
        if (extraComponents[extra] == transformId) continue;
        if (std::find(extraComponents.begin(), extraComponents.begin() + extra, extraComponents[extra]) != extraComponents.begin() + extra) continue;
        std::uint8_t componentExtraMask = 0U;
        for (std::size_t alias = extra; alias < extraComponents.size(); ++alias) {
            if (extraComponents[alias] == extraComponents[extra]) componentExtraMask |= static_cast<std::uint8_t>(1U << alias);
        }
        auto& writes = arena.writes;
        writes.clear();
        for (std::size_t index = 0U; index < records.size(); ++index) {
            const auto& record = records[index];
            const auto& written = rangeWrites[index];
            if ((written.fullExtraMask & componentExtraMask) != 0U) {
                writes.push_back({ record.nativeArchetypeIndex, record.nativeChunkIndex, 0U, record.entityCount });
            } else {
                for (const auto& rowsWritten : written.extraRows) {
                    if ((componentExtraMask & (1U << rowsWritten.extraIndex)) != 0U) {
                        writes.push_back({ record.nativeArchetypeIndex, record.nativeChunkIndex, rowsWritten.firstRow, rowsWritten.count });
                    }
                }
            }
        }
        std::sort(writes.begin(), writes.end(), [](const auto& left, const auto& right) {
            if (left.archetypeIndex != right.archetypeIndex) return left.archetypeIndex < right.archetypeIndex;
            if (left.chunkIndex != right.chunkIndex) return left.chunkIndex < right.chunkIndex;
            return left.firstRow < right.firstRow;
        });
        std::size_t uniqueRuns = 0U;
        for (const auto& run : writes) {
            if (uniqueRuns != 0U) {
                auto& previous = writes[uniqueRuns - 1U];
                if (previous.archetypeIndex == run.archetypeIndex && previous.chunkIndex == run.chunkIndex &&
                    run.firstRow <= previous.firstRow + previous.count) {
                    previous.count = std::max(previous.firstRow + previous.count, run.firstRow + run.count) - previous.firstRow;
                    continue;
                }
            }
            writes[uniqueRuns++] = run;
        }
        writes.resize(uniqueRuns);
        if (writes.empty()) continue;
        storage.MarkComponentRowsModified(extraComponents[extra], writes);
        if (state.world.MirrorsValueWrites(extraComponents[extra])) {
            std::size_t recordIndex = 0U;
            for (const auto& run : writes) {
                while (recordIndex < records.size() && (records[recordIndex].nativeArchetypeIndex != run.archetypeIndex ||
                    records[recordIndex].nativeChunkIndex != run.chunkIndex)) ++recordIndex;
                if (recordIndex == records.size()) throw std::logic_error("An extra-column write lost its transform range");
                for (std::size_t row = run.firstRow; row < run.firstRow + run.count; ++row) {
                    observedExtraRows[extra].push_back(kb::ecs::Entity{ records[recordIndex].entityIds[row] });
                }
            }
        }
    }
    for (std::size_t extra = 0U; extra < extraComponents.size(); ++extra) {
        for (const kb::ecs::Entity entity : observedExtraRows[extra]) {
            if (storage.TryGetComponentData(entity, extraComponents[extra]) != nullptr) {
                kb::ecs::WorldInternalAccess::MarkComponentModified(state.world, entity, extraComponents[extra]);
            }
        }
    }
    const auto publishDeferred = [&state, observed, trackPrefab](const TransformPassOutput& output) {
        for (const auto& deferred : output.deferred) {
            if (observed) state.componentStorage.Transforms().MarkWritten(deferred.entity);
            if (SceneHierarchyCache::HasTransformLink(state, deferred.entity)) EnqueueSceneTransformDirtyFrontier(state, deferred.entity);
            if (trackPrefab) MarkScenePrefabNodeDirty(state, deferred.entity);
        }
    };
    // The arena lease survives every observer callback and any nested pass until publication is complete.
    for (const auto& output : outputs) publishDeferred(output);
    publishDeferred(arena.aliasOutput);
    state.transformRenderProxyListsStale = true;
    if (firstException != nullptr) std::rethrow_exception(firstException);
    return stats;
}

bool SceneTransformComposesOnWrite(const SceneState& state, SceneEntity entity) noexcept {
    if (SceneHierarchyCache::HasTransformLink(state, entity)) return false;
    const std::uint8_t mask = SceneRenderProxyComponentMaskOf(state, entity);
    return !SceneRenderProxyMaskHas(mask, SceneRenderProxyComponentMask::Camera) && !SceneRenderProxyMaskHas(mask, SceneRenderProxyComponentMask::Light);
}

void ComposeSceneTransformRoot(TransformComponent& transform) noexcept {
    if (SceneTransformRootHotKernel::CanApplyIdentityRotationFastPath(transform)) {
        SceneTransformRootHotKernel::ApplyIdentityRotationRoot(transform);
    } else {
        transform = TransformMath::ComposeRoot(transform);
    }
}

void RecordComposedSceneTransform(SceneState& state, SceneEntity entity, const TransformComponent& transform) {
    RecordUpdatedTransform(state, entity, &transform);
}

void PrepareSceneTransformUpdateRecording(SceneState& state) {
    BeginSceneTransformRenderProxyUpdates(state);
    if (const std::size_t words = (state.denseHierarchyParents.size() + 63U) / 64U; state.transformUpdatedBits.size() < words) {
        state.transformUpdatedBits.resize(words, 0U);
    }
    state.transformRenderProxyListsStale = true;
}

void BeginSceneTransformRenderProxyUpdates(SceneState& state) noexcept {
    if (!state.transformUpdatesResetPending) {
        return;
    }
    state.transformUpdatesResetPending = false;
    std::ranges::fill(state.transformUpdatedBits, 0U);
    state.transformUpdatedSparseEntities.clear();
    state.lastTransformRenderProxyIdentityAffineFastPathCount = 0U;
    state.transformRenderProxyListsStale = true;
}

SceneTransformRenderProxyCounts CountSceneTransformRenderProxyUpdates(const SceneState& state) {
    SceneTransformRenderProxyCounts counts{ .identityAffine = state.lastTransformRenderProxyIdentityAffineFastPathCount };
    for (const std::uint64_t word : state.transformUpdatedBits) {
        counts.updated += static_cast<std::size_t>(std::popcount(word));
    }
    for (const SceneEntity entity : state.transformUpdatedSparseEntities) {
        counts.updated += state.componentStorage.Transforms().TryGet(entity) != nullptr ? 1U : 0U;
    }
    // The mesh, camera and light entries among the updated ones, found by their components.
    struct Context {
        const SceneState& state;
        SceneTransformRenderProxyCounts& counts;
    } context{ state, counts };
    SceneComponentIteration::ForEachMeshRenderer(state.world, state.components.TransformComponentId(), state.components.MeshRendererComponentId(),
        state.ComponentIterationQueries(), [](SceneEntity entity, const TransformComponent&, const MeshRendererComponent&, void* context) {
            auto& data = *static_cast<Context*>(context);
            if (!IsTransformUpdated(data.state, entity)) return;
            ++data.counts.meshRenderers;
            data.counts.visibleMeshRenderers += SceneRenderProxyMaskHas(SceneRenderProxyComponentMaskOf(data.state, entity), SceneRenderProxyComponentMask::Hidden) ? 0U : 1U;
        }, &context);
    SceneComponentIteration::ForEachCamera(state.world, state.components.TransformComponentId(), state.components.CameraComponentId(),
        state.ComponentIterationQueries(), [](SceneEntity entity, const TransformComponent&, const CameraComponent&, void* context) {
            auto& data = *static_cast<Context*>(context);
            data.counts.cameras += IsTransformUpdated(data.state, entity) ? 1U : 0U;
        }, &context);
    SceneComponentIteration::ForEachLight(state.world, state.components.TransformComponentId(), state.components.LightComponentId(),
        state.ComponentIterationQueries(), [](SceneEntity entity, const TransformComponent&, const LightComponent&, void* context) {
            auto& data = *static_cast<Context*>(context);
            data.counts.lights += IsTransformUpdated(data.state, entity) ? 1U : 0U;
        }, &context);
    return counts;
}

// Lists the entities whose update bit is set, with their world affines, in the order of their dense indices. The words of the
// bit set are split in chunks; the workers fill their own part of the lists (each resolves its entities from their slots, so
// only the set bits are visited), and the parts are then packed together: an entity destroyed since it was updated leaves
// a gap in its part.
void AppendDenseRenderProxyEntries(SceneState& state) {
    constexpr std::size_t kWordsPerChunk = 256U;
    constexpr std::size_t kParallelBitCount = 4096U;
    const std::vector<std::uint64_t>& bits = state.transformUpdatedBits;
    const std::size_t chunkCount = (bits.size() + kWordsPerChunk - 1U) / kWordsPerChunk;
    std::vector<SceneTransformProxyChunk>& chunks = state.transformRenderProxyChunksScratch;
    chunks.assign(chunkCount, SceneTransformProxyChunk{});
    std::size_t bound = 0U;
    for (std::size_t chunk = 0U; chunk < chunkCount; ++chunk) {
        chunks[chunk].boundFirst = bound;
        for (std::size_t word = chunk * kWordsPerChunk; word < std::min(bits.size(), (chunk + 1U) * kWordsPerChunk); ++word) {
            chunks[chunk].bound += static_cast<std::size_t>(std::popcount(bits[word]));
        }
        bound += chunks[chunk].bound;
    }
    if (bound == 0U) {
        return;
    }
    auto& entities = state.transformRenderProxyUpdateEntities;
    auto& affines = state.transformRenderProxyWorldAffine3x4;
    entities.resize(bound);
    affines.resize(bound);
    const kb::ecs::NativeArchetypeStorage& storage = state.world.NativeStorage();
    const kb::ecs::ComponentId transformId = state.components.TransformComponentId();
    const auto runChunks = [&state, chunkCount, bound](auto&& body) {
        if (bound < kParallelBitCount) {
            body(kb::ecs::WorkerContext{}, kb::ecs::WorkerPoolChunk{ .index = 0U, .begin = 0U, .count = chunkCount });
            return;
        }
        EnsureWorkerPool(state);
        state.transformWorkerPool->ParallelForChunks(chunkCount, 1U, body);
    };
    runChunks([&](kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk& range) {
        for (std::size_t index = range.begin; index < range.begin + range.count; ++index) {
            SceneTransformProxyChunk& out = chunks[index];
            std::size_t write = out.boundFirst;
            for (std::size_t word = index * kWordsPerChunk; word < std::min(bits.size(), (index + 1U) * kWordsPerChunk); ++word) {
                for (std::uint64_t remaining = bits[word]; remaining != 0U; remaining &= remaining - 1U) {
                    const std::uint64_t slot = word * 64U + static_cast<std::uint64_t>(std::countr_zero(remaining));
                    const SceneEntity entity = storage.ResolveAliveEntity(kb::ecs::kGeneratedEntityIndexBase + slot);
                    if (!entity.IsValid()) continue;
                    const auto* transform = static_cast<const TransformComponent*>(storage.TryGetComponentData(entity, transformId));
                    if (transform == nullptr) continue;
                    entities[write] = entity;
                    affines[write] = BuildWorldAffine3x4(*transform);
                    ++write;
                    const std::uint8_t mask = SceneRenderProxyComponentMaskOf(state, entity);
                    const bool mesh = SceneRenderProxyMaskHas(mask, SceneRenderProxyComponentMask::MeshRenderer);
                    out.meshCount += mesh ? 1U : 0U;
                    out.visibleMeshCount += mesh && !SceneRenderProxyMaskHas(mask, SceneRenderProxyComponentMask::Hidden) ? 1U : 0U;
                    out.cameraCount += SceneRenderProxyMaskHas(mask, SceneRenderProxyComponentMask::Camera) ? 1U : 0U;
                    out.lightCount += SceneRenderProxyMaskHas(mask, SceneRenderProxyComponentMask::Light) ? 1U : 0U;
                }
            }
            out.written = write - out.boundFirst;
        }
    });

    std::size_t total = 0U;
    std::size_t meshTotal = 0U;
    std::size_t visibleTotal = 0U;
    std::size_t cameraTotal = 0U;
    std::size_t lightTotal = 0U;
    for (SceneTransformProxyChunk& chunk : chunks) {
        chunk.first = total;
        chunk.meshFirst = meshTotal;
        chunk.visibleMeshFirst = visibleTotal;
        chunk.cameraFirst = cameraTotal;
        chunk.lightFirst = lightTotal;
        if (chunk.first != chunk.boundFirst) {
            std::copy_n(entities.begin() + static_cast<std::ptrdiff_t>(chunk.boundFirst), chunk.written, entities.begin() + static_cast<std::ptrdiff_t>(chunk.first));
            std::copy_n(affines.begin() + static_cast<std::ptrdiff_t>(chunk.boundFirst), chunk.written, affines.begin() + static_cast<std::ptrdiff_t>(chunk.first));
        }
        total += chunk.written;
        meshTotal += chunk.meshCount;
        visibleTotal += chunk.visibleMeshCount;
        cameraTotal += chunk.cameraCount;
        lightTotal += chunk.lightCount;
    }
    entities.resize(total);
    affines.resize(total);
    if (meshTotal + cameraTotal + lightTotal == 0U) {
        return;
    }
    state.transformRenderProxyMeshRendererIndices.resize(meshTotal);
    state.transformRenderProxyVisibleMeshRendererIndices.resize(visibleTotal);
    state.transformRenderProxyCameraIndices.resize(cameraTotal);
    state.transformRenderProxyLightIndices.resize(lightTotal);
    runChunks([&](kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk& range) {
        for (std::size_t index = range.begin; index < range.begin + range.count; ++index) {
            const SceneTransformProxyChunk& chunk = chunks[index];
            std::size_t meshWrite = chunk.meshFirst;
            std::size_t visibleWrite = chunk.visibleMeshFirst;
            std::size_t cameraWrite = chunk.cameraFirst;
            std::size_t lightWrite = chunk.lightFirst;
            for (std::size_t proxyIndex = chunk.first; proxyIndex < chunk.first + chunk.written; ++proxyIndex) {
                const std::uint8_t mask = SceneRenderProxyComponentMaskOf(state, entities[proxyIndex]);
                if (SceneRenderProxyMaskHas(mask, SceneRenderProxyComponentMask::MeshRenderer)) {
                    state.transformRenderProxyMeshRendererIndices[meshWrite++] = proxyIndex;
                    if (!SceneRenderProxyMaskHas(mask, SceneRenderProxyComponentMask::Hidden)) {
                        state.transformRenderProxyVisibleMeshRendererIndices[visibleWrite++] = proxyIndex;
                    }
                }
                if (SceneRenderProxyMaskHas(mask, SceneRenderProxyComponentMask::Camera)) state.transformRenderProxyCameraIndices[cameraWrite++] = proxyIndex;
                if (SceneRenderProxyMaskHas(mask, SceneRenderProxyComponentMask::Light)) state.transformRenderProxyLightIndices[lightWrite++] = proxyIndex;
            }
        }
    });
}

void EnsureSceneTransformRenderProxyLists(const SceneState& constState) {
    if (!constState.transformRenderProxyListsStale) {
        return;
    }
    // A derived view, built on the first read after the transforms changed: the scene state is not otherwise changed.
    SceneState& state = const_cast<SceneState&>(constState);
    state.transformRenderProxyListsStale = false;
    state.transformRenderProxyUpdateEntities.clear();
    state.transformRenderProxyWorldAffine3x4.clear();
    state.transformRenderProxyMeshRendererIndices.clear();
    state.transformRenderProxyVisibleMeshRendererIndices.clear();
    state.transformRenderProxyCameraIndices.clear();
    state.transformRenderProxyLightIndices.clear();
    AppendDenseRenderProxyEntries(state);
    const std::size_t denseCount = state.transformRenderProxyUpdateEntities.size();
    for (const SceneEntity entity : state.transformUpdatedSparseEntities) {
        if (const TransformComponent* transform = state.componentStorage.Transforms().TryGet(entity); transform != nullptr) {
            AppendRenderProxyListEntry(state, entity, *transform);
        }
    }
    for (std::size_t proxyIndex = denseCount; proxyIndex < state.transformRenderProxyUpdateEntities.size(); ++proxyIndex) {
        const std::uint8_t componentMask = SceneRenderProxyComponentMaskOf(state, state.transformRenderProxyUpdateEntities[proxyIndex]);
        if (SceneRenderProxyMaskHas(componentMask, SceneRenderProxyComponentMask::MeshRenderer)) {
            state.transformRenderProxyMeshRendererIndices.push_back(proxyIndex);
            if (!SceneRenderProxyMaskHas(componentMask, SceneRenderProxyComponentMask::Hidden)) {
                state.transformRenderProxyVisibleMeshRendererIndices.push_back(proxyIndex);
            }
        }
        if (SceneRenderProxyMaskHas(componentMask, SceneRenderProxyComponentMask::Camera)) {
            state.transformRenderProxyCameraIndices.push_back(proxyIndex);
        }
        if (SceneRenderProxyMaskHas(componentMask, SceneRenderProxyComponentMask::Light)) {
            state.transformRenderProxyLightIndices.push_back(proxyIndex);
        }
    }
}

void SceneTransformHierarchySystem::Update(SceneState& state) const {
    using Clock = std::chrono::steady_clock;

    const auto updateStart = Clock::now();
    const TransformComponent identity = TransformMath::Identity();
    RootSyncProfileTimings rootSyncProfile;
    RootSyncProfileTimings* const profileTimings = RootSyncProfileEnabled() ? &rootSyncProfile : nullptr;
    const auto topologyStart = profileTimings != nullptr ? Clock::now() : Clock::time_point{};
    // Structural changes can remove queued handles and introduce transforms
    // outside the dirty frontier. Rebuild from the live hierarchy in that case.
    const bool topologyChanged = state.transformTopology.Version() != state.hierarchyTopologyVersion;
    if (topologyChanged) {
        ClearSceneTransformDirtyFrontier(state);
    }
    state.transformTopology.Refresh(state);
    if (profileTimings != nullptr) {
        profileTimings->topologyNanoseconds = Nanoseconds(Clock::now() - topologyStart);
    }
    if (state.transformPropagationCursorVersion != state.hierarchyTopologyVersion ||
        state.transformPropagationCursorLevel >= state.transformTopology.Levels().size()) {
        ResetPropagationCursor(state);
    }

    state.transformDirtyScratch.clear();
    state.transformWorldScratch.clear();
    // The composition below may write world residuals from worker threads: size their table first. A scene that
    // has none (everything near the origin) never allocates it.
    if (!state.transformResiduals.Empty()) state.transformResiduals.Reserve(state.denseHierarchyParents.size());
    const std::size_t trackedSlotCount = HierarchyTrackedSlotCount(state);
    state.transformDirtyScratch.reserve(state.hierarchyOrder.size());
    state.transformWorldScratch.reserve(state.hierarchyOrder.size());
    state.lastTransformHierarchyInspectedCount = 0U;
    state.lastTransformHierarchyUpdatedCount = 0U;
    state.lastTransformHierarchyRootFastPathCount = 0U;
    state.lastTransformHierarchyTranslatedParentFastPathCount = 0U;
    state.lastTransformHierarchyUnrotatedParentFastPathCount = 0U;
    state.lastTransformHierarchyUnitScaleParentFastPathCount = 0U;
    state.lastTransformHierarchyUniformScaleParentFastPathCount = 0U;
    state.lastTransformHierarchyStaticLocalRotationFastPathCount = 0U;
    state.lastTransformHierarchySparseFlushCount = 0U;
    state.lastTransformHierarchyDirtyListFlushCount = 0U;
    state.lastTransformHierarchyDirtyListFlushEntityCount = 0U;
    state.lastTransformHierarchyBatchFlushCount = 0U;
    state.lastTransformHierarchyFlushedEntityCount = 0U;
    state.lastTransformHierarchyDirtyFrontierCount = 0U;
    state.lastTransformHierarchyParallelBatchCount = 0U;
    state.lastTransformHierarchyParallelChunkCount = 0U;
    state.lastTransformHierarchyParallelEntityCount = 0U;
    state.lastTransformHierarchyWorkerCount = 1U;
    state.lastTransformHierarchyParallelFlushCount = 0U;
    state.lastTransformHierarchyParallelFlushChunkCount = 0U;
    state.lastTransformHierarchyParallelFlushEntityCount = 0U;
    state.lastTransformHierarchyParallelFlushWorkerCount = 1U;
    state.lastTransformHierarchyCacheBuildNanoseconds = 0U;
    state.lastTransformHierarchyEntryBuildNanoseconds = 0U;
    state.lastTransformHierarchyKernelApplyNanoseconds = 0U;
    state.lastTransformHierarchyFrontierAppendNanoseconds = 0U;
    state.lastTransformHierarchyPropagateNanoseconds = 0U;
    state.lastTransformHierarchyFlushWriteNanoseconds = 0U;
    state.lastTransformHierarchyBackendMarkNanoseconds = 0U;
    state.lastTransformHierarchyUpdateNanoseconds = 0U;
    state.lastTransformHierarchyFlushNanoseconds = 0U;
    state.lastTransformHierarchyBudgetExhausted = false;
    state.transformHierarchyUpdatedEntitiesScratch.clear();
    state.transformHierarchyUpdatedTransformsScratch.clear();
    if (const std::size_t words = (state.denseHierarchyParents.size() + 63U) / 64U; state.transformUpdatedBits.size() < words) {
        state.transformUpdatedBits.resize(words, 0U);
    }
    if (RunNativeDirtyRanges(state, updateStart, profileTimings, topologyChanged)) {
        return;
    }

    // The native lane reads and composes component rows directly. Dense caches
    // are needed only by the generic hierarchy lanes below, including their
    // first use after a root-only scene acquires a parent or a bounded budget.
    const auto denseScratchStart = profileTimings != nullptr ? Clock::now() : Clock::time_point{};
    PrepareDenseTransformScratch(state);
    if (profileTimings != nullptr) {
        profileTimings->denseScratchNanoseconds = Nanoseconds(Clock::now() - denseScratchStart);
    }

    std::vector<SceneTransformBatchEntry>& entries = state.transformHierarchyEntriesScratch;
    std::vector<SceneEntity>& updatedEntities = state.transformHierarchyUpdatedEntitiesScratch;
    entries.clear();
    // Rows the native lane already composed stay first; the generic lanes append the linked rows they update.
    const std::size_t nativeUpdatedCount = updatedEntities.size();
    const auto genericUpdated = [&updatedEntities, nativeUpdatedCount] { return std::span<const SceneEntity>{ updatedEntities }.subspan(nativeUpdatedCount); };
    const auto recordGenericUpdates = [&state, &genericUpdated] {
        for (const SceneEntity entity : genericUpdated()) RecordUpdatedTransform(state, entity, state.componentStorage.Transforms().TryGet(entity));
        if (!genericUpdated().empty()) PublishRenderProxyTransformUpdates(state);
    };
    const std::size_t budgetLimit = state.transformPropagationBudget.maxInspectedEntitiesPerSync;

    const auto cacheBuildStart = Clock::now();
    TransformValueCache transformValues = BuildTransformValueCache(state);
    const auto cacheBuildEnd = Clock::now();
    state.lastTransformHierarchyCacheBuildNanoseconds = Nanoseconds(cacheBuildEnd - cacheBuildStart);
    PrewarmTransformScratchForCompletedLevels(state, transformValues);

    kb::ecs::ReserveGeometric(updatedEntities, nativeUpdatedCount + trackedSlotCount);

    std::size_t remainingBudget = budgetLimit;
    bool completed = true;
    for (std::size_t levelIndex = state.transformPropagationCursorLevel; levelIndex < state.transformTopology.Levels().size(); ++levelIndex) {
        const std::vector<SceneEntity>& level = state.transformTopology.Levels()[levelIndex];
        const std::size_t levelBegin = levelIndex == state.transformPropagationCursorLevel ? state.transformPropagationCursorOffset : 0U;
        if (levelBegin >= level.size()) {
            continue;
        }
        if (budgetLimit > 0U && remainingBudget == 0U) {
            state.transformPropagationCursorLevel = levelIndex;
            state.transformPropagationCursorOffset = levelBegin;
            state.lastTransformHierarchyBudgetExhausted = true;
            completed = false;
            break;
        }

        const std::size_t candidateCount = level.size() - levelBegin;
        const std::size_t batchEntityCount = budgetLimit == 0U ? candidateCount : std::min(candidateCount, remainingBudget);
        const std::span<const SceneEntity> levelSlice{ level.data() + static_cast<std::ptrdiff_t>(levelBegin), batchEntityCount };
        entries.clear();
        kb::ecs::ReserveGeometric(entries, levelSlice.size());

        const auto entryBuildStart = Clock::now();
        for (const SceneEntity entity : levelSlice) {
            AppendTransformEntryIfDirty(state, transformValues, identity, entity, entries);
        }
        state.lastTransformHierarchyEntryBuildNanoseconds += Nanoseconds(Clock::now() - entryBuildStart);

        if (entries.empty()) {
            if (budgetLimit > 0U) {
                remainingBudget -= batchEntityCount;
            }
            if (levelBegin + batchEntityCount < level.size()) {
                state.transformPropagationCursorLevel = levelIndex;
                state.transformPropagationCursorOffset = levelBegin + batchEntityCount;
                state.lastTransformHierarchyBudgetExhausted = true;
                completed = false;
                break;
            }
            continue;
        }

        ApplyTransformEntries(state, transformValues, entries, updatedEntities);

        if (budgetLimit > 0U) {
            remainingBudget -= batchEntityCount;
        }
        if (levelBegin + batchEntityCount < level.size()) {
            state.transformPropagationCursorLevel = levelIndex;
            state.transformPropagationCursorOffset = levelBegin + batchEntityCount;
            state.lastTransformHierarchyBudgetExhausted = true;
            completed = false;
            break;
        }
        state.transformPropagationCursorLevel = levelIndex + 1U;
        state.transformPropagationCursorOffset = 0U;
    }

    if (completed) {
        ResetPropagationCursor(state);
    }
    const auto flushStart = Clock::now();
    state.lastTransformHierarchyUpdateNanoseconds = Nanoseconds(flushStart - updateStart);
    state.lastTransformHierarchyPropagateNanoseconds = Nanoseconds(flushStart - cacheBuildEnd);
    const TransformFlushStats flushStats = FlushDirtyTransforms(state, transformValues, genericUpdated());
    const auto flushEnd = Clock::now();
    state.lastTransformHierarchyFlushNanoseconds = Nanoseconds(flushEnd - flushStart);
    state.lastTransformHierarchyFlushWriteNanoseconds = flushStats.writeNanoseconds;
    state.lastTransformHierarchyBackendMarkNanoseconds = flushStats.backendMarkNanoseconds;
    state.lastTransformHierarchySparseFlushCount = flushStats.sparseFlushCount;
    state.lastTransformHierarchyDirtyListFlushCount = flushStats.dirtyListFlushCount;
    state.lastTransformHierarchyDirtyListFlushEntityCount = flushStats.dirtyListFlushEntityCount;
    state.lastTransformHierarchyBatchFlushCount = flushStats.batchFlushCount;
    state.lastTransformHierarchyFlushedEntityCount = flushStats.flushedEntityCount;
    state.lastTransformHierarchyParallelFlushCount = flushStats.parallelFlushCount;
    state.lastTransformHierarchyParallelFlushChunkCount = flushStats.parallelFlushChunkCount;
    state.lastTransformHierarchyParallelFlushEntityCount = flushStats.parallelFlushEntityCount;
    state.lastTransformHierarchyParallelFlushWorkerCount = flushStats.parallelFlushWorkerCount;
    recordGenericUpdates();
    ClearSceneTransformDirtyFrontier(state);
    PrintRootSyncProfile(state, profileTimings, updatedEntities.size(), updateStart);
}

} // namespace kb::scene
