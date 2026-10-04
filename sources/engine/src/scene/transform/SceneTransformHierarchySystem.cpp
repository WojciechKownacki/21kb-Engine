#include "scene/transform/SceneTransformHierarchySystem.hpp"

#include "engine/ecs/Query.hpp"
#include "engine/ecs/UnsafeHotQuery.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "ecs/GeometricReserve.hpp"
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
#include "scene/transform/SceneTransformLeafBatchUpdater.hpp"
#include "scene/transform/TransformMath.hpp"

#include <algorithm>
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

void AdvanceTransformValueCacheLoadMarkEpoch(SceneState& state) noexcept {
    state.transformValueCacheLoadEntitiesScratch.clear();
    if (state.transformValueCacheLoadMarkEpoch == std::numeric_limits<std::uint32_t>::max()) {
        state.transformValueCacheLoadMarkEpoch = 1U;
        std::ranges::fill(state.transformValueCacheLoadDenseMarkEpochs, 0U);
        state.transformValueCacheLoadSparseMarkEpochs.clear();
        return;
    }
    ++state.transformValueCacheLoadMarkEpoch;
}

[[nodiscard]] bool IsTransformValueCacheLoadMarked(const SceneState& state, SceneEntity entity) noexcept {
    const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(entity);
    if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex) {
        return denseIndex < state.transformValueCacheLoadDenseMarkEpochs.size()
            && state.transformValueCacheLoadDenseMarkEpochs[denseIndex] == state.transformValueCacheLoadMarkEpoch
            && denseIndex < state.transformValueCacheLoadDenseMarkedEntities.size()
            && state.transformValueCacheLoadDenseMarkedEntities[denseIndex] == entity;
    }

    const auto mark = state.transformValueCacheLoadSparseMarkEpochs.find(entity.Id());
    return mark != state.transformValueCacheLoadSparseMarkEpochs.end() && mark->second == state.transformValueCacheLoadMarkEpoch;
}

void EnqueueTransformValueCacheLoadCandidate(SceneState& state, SceneEntity entity) {
    if (!entity.IsValid()) {
        return;
    }

    const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(entity);
    if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex) {
        const std::size_t requiredSize = static_cast<std::size_t>(denseIndex) + 1U;
        if (state.transformValueCacheLoadDenseMarkEpochs.size() < requiredSize) {
            state.transformValueCacheLoadDenseMarkEpochs.resize(requiredSize, 0U);
            state.transformValueCacheLoadDenseMarkedEntities.resize(requiredSize);
        }
        if (IsTransformValueCacheLoadMarked(state, entity)) {
            return;
        }
        state.transformValueCacheLoadDenseMarkEpochs[denseIndex] = state.transformValueCacheLoadMarkEpoch;
        state.transformValueCacheLoadDenseMarkedEntities[denseIndex] = entity;
        state.transformValueCacheLoadEntitiesScratch.push_back(entity);
        return;
    }

    auto mark = state.transformValueCacheLoadSparseMarkEpochs.find(entity.Id());
    if (mark != state.transformValueCacheLoadSparseMarkEpochs.end() && mark->second == state.transformValueCacheLoadMarkEpoch) {
        return;
    }
    if (mark == state.transformValueCacheLoadSparseMarkEpochs.end()) {
        state.transformValueCacheLoadSparseMarkEpochs.emplace(entity.Id(), state.transformValueCacheLoadMarkEpoch);
    } else {
        mark->second = state.transformValueCacheLoadMarkEpoch;
    }
    state.transformValueCacheLoadEntitiesScratch.push_back(entity);
}

void AddTransformCacheEntryFromHotBatch(
    TransformCacheBuildContext& buildContext,
    SceneEntity entity,
    const TransformComponent& transform) {
    TransformValueCache& transformCache = *buildContext.cache;
    const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(entity);
    if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex && denseIndex < transformCache.denseLimit) {
        transformCache.dense[denseIndex] = SceneTransformValueCacheEntry{
            .entity = entity,
            .transform = transform,
            .cacheVersion = transformCache.buildVersion,
            .valid = true,
            .dirty = false,
        };
        return;
    }

    {
        std::lock_guard lock{ buildContext.sparseMutex };
        transformCache.sparse[entity.Id()] = SceneTransformValueCacheEntry{
            .entity = entity,
            .transform = transform,
            .cacheVersion = transformCache.buildVersion,
            .valid = true,
            .dirty = false,
        };
    }
}

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

[[nodiscard]] bool CanUseHierarchyDirtyFrontier(const SceneState& state, const TransformValueCache& transformValues) noexcept {
    if (state.transformDirtyFrontierEntities.empty() || state.transformPropagationBudget.maxInspectedEntitiesPerSync > 0U
        || state.transformPropagationCursorLevel != 0U || state.transformPropagationCursorOffset != 0U) {
        return false;
    }

    for (const SceneEntity entity : state.transformDirtyFrontierEntities) {
        if (!entity.IsValid() || transformValues.Find(entity) == nullptr) {
            return false;
        }
        std::size_t guard = HierarchyTrackedSlotCount(state) + state.transformDirtyFrontierEntities.size() + 1U;
        SceneEntity parent = ParentOf(state, entity);
        while (parent.IsValid() && guard-- > 0U) {
            const TransformComponent* parentTransform = transformValues.Find(parent);
            if (parentTransform == nullptr) {
                return false;
            }
            if (IsSceneTransformDirtyFrontierMarked(state, parent)) {
                break;
            }
            if (parentTransform->worldDirty) {
                return false;
            }
            parent = ParentOf(state, parent);
        }
        if (guard == 0U && parent.IsValid()) {
            return false;
        }
    }
    return true;
}

void AddTransformCacheEntryFromSparseLookup(TransformValueCache& cache, const SceneState& state, SceneEntity entity) {
    if (!entity.IsValid() || cache.Find(entity) != nullptr) {
        return;
    }
    const TransformComponent* transform = SceneComponentStorageAccess::TryGet<TransformComponent>(&state.world, entity);
    if (transform != nullptr) {
        cache.Add(entity, *transform);
    }
}

[[nodiscard]] TransformValueCache BuildDirtyFrontierTransformValueCache(SceneState& state) {
    TransformValueCache cache = BeginTransformValueCache(state);
    AdvanceTransformValueCacheLoadMarkEpoch(state);
    kb::ecs::ReserveGeometric(state.transformValueCacheLoadEntitiesScratch, std::min<std::size_t>(
        state.hierarchyOrder.size(),
        std::max<std::size_t>(state.transformDirtyFrontierEntities.size() * 4U, 16U)));

    std::vector<SceneEntity>& subtreeStack = state.transformDirtyFrontierLevelScratch;
    subtreeStack.clear();
    kb::ecs::ReserveGeometric(subtreeStack, state.transformDirtyFrontierEntities.size());
    for (const SceneEntity entity : state.transformDirtyFrontierEntities) {
        std::size_t guard = HierarchyTrackedSlotCount(state) + state.transformDirtyFrontierEntities.size() + 1U;
        SceneEntity cursor = entity;
        while (cursor.IsValid() && guard-- > 0U) {
            EnqueueTransformValueCacheLoadCandidate(state, cursor);
            cursor = ParentOf(state, cursor);
        }
        if (!HasDirtyAncestorInFrontier(state, entity)) {
            subtreeStack.push_back(entity);
        }
    }

    while (!subtreeStack.empty()) {
        const SceneEntity entity = subtreeStack.back();
        subtreeStack.pop_back();
        EnqueueTransformValueCacheLoadCandidate(state, entity);
        for (const SceneEntity child : ChildrenOf(state, entity)) {
            subtreeStack.push_back(child);
        }
    }

    const std::size_t candidateCount = state.transformValueCacheLoadEntitiesScratch.size();
    if (candidateCount == 0U) {
        return cache;
    }

    cache.sparse.reserve(std::min<std::size_t>(state.hierarchyOrder.size(), candidateCount));
    const std::size_t trackedCount = HierarchyTrackedSlotCount(state);
    if (candidateCount <= kTransformBatchGrainSize * 2U || candidateCount * kSparseTransformFlushLookupFactor < trackedCount) {
        for (const SceneEntity entity : state.transformValueCacheLoadEntitiesScratch) {
            AddTransformCacheEntryFromSparseLookup(cache, state, entity);
        }
        cache.liveCount = trackedCount;
        return cache;
    }

    kb::ecs::Query<TransformComponent> query = state.world.CreateQuery<TransformComponent>();
    if (!query.IsValid()) {
        return cache;
    }

    kb::ecs::QueryExecutionSettings settings;
    settings.maxBatchSize = kTransformBatchGrainSize;
    settings.policy = kb::ecs::QueryExecutionPolicy::SingleThread;
    if (candidateCount > kTransformBatchGrainSize * 32U) {
        EnsureWorkerPool(state);
        settings.policy = kb::ecs::QueryExecutionPolicy::ParallelChunks;
        settings.workerPool = state.transformWorkerPool.get();
    }

    kb::ecs::UnsafeHotReadQuery<TransformComponent> hotQuery;
    if (!hotQuery.Rebuild(query, settings)) {
        return cache;
    }

    TransformCacheBuildContext context{ .cache = &cache };
    auto loadBatch = [&state](const auto& batch, TransformCacheBuildContext& buildContext) {
        const auto* transforms = batch.template Components<0>();
        for (std::size_t row = 0; row < batch.Count(); ++row) {
            const SceneEntity entity = batch.EntityAt(row);
            if (IsTransformValueCacheLoadMarked(state, entity)) {
                AddTransformCacheEntryFromHotBatch(buildContext, entity, transforms[row]);
            }
        }
    };

    if (settings.workerPool != nullptr && settings.policy == kb::ecs::QueryExecutionPolicy::ParallelChunks) {
        hotQuery.ForEachRangeParallel(settings.maxBatchSize, *settings.workerPool, settings.workerCountOverride, [&context, &loadBatch](const auto& batch, kb::ecs::WorkerContext) {
            loadBatch(batch, context);
        });
    } else {
        hotQuery.ForEachRange(settings.maxBatchSize, [&context, &loadBatch](const auto& batch) {
            loadBatch(batch, context);
        });
    }

    cache.liveCount = trackedCount;
    return cache;
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

void AppendUpdatedChildrenToFrontier(
    const SceneState& state,
    const std::vector<SceneTransformBatchEntry>& entries,
    std::vector<SceneEntity>& nextFrontier) {
    std::size_t childCount = 0U;
    for (const SceneTransformBatchEntry& entry : entries) {
        if (!entry.updated) {
            continue;
        }
        const std::span<const SceneEntity> children = ChildrenOf(state, entry.entity);
        childCount += children.size();
    }
    if (childCount == 0U) {
        return;
    }

    const std::size_t writeBegin = nextFrontier.size();
    nextFrontier.resize(writeBegin + childCount);
    std::size_t writeCursor = writeBegin;
    for (const SceneTransformBatchEntry& entry : entries) {
        if (!entry.updated) {
            continue;
        }
        const std::span<const SceneEntity> children = ChildrenOf(state, entry.entity);
        std::ranges::copy(children, nextFrontier.begin() + static_cast<std::ptrdiff_t>(writeCursor));
        writeCursor += children.size();
    }
}

void RunHierarchyDirtyFrontier(
    SceneState& state,
    TransformValueCache& transformValues,
    const TransformComponent& identity,
    std::vector<SceneTransformBatchEntry>& entries,
    std::vector<SceneEntity>& updatedEntities) {
    std::vector<SceneEntity>& currentFrontier = state.transformDirtyFrontierLevelScratch;
    std::vector<SceneEntity>& nextFrontier = state.transformDirtyFrontierNextScratch;
    currentFrontier.clear();
    nextFrontier.clear();
    kb::ecs::ReserveGeometric(currentFrontier, state.transformDirtyFrontierEntities.size());
    for (const SceneEntity entity : state.transformDirtyFrontierEntities) {
        if (HasDirtyAncestorInFrontier(state, entity)) {
            continue;
        }
        currentFrontier.push_back(entity);
    }

    while (!currentFrontier.empty()) {
        entries.clear();
        kb::ecs::ReserveGeometric(entries, currentFrontier.size());
        const auto entryBuildStart = std::chrono::steady_clock::now();
        for (const SceneEntity entity : currentFrontier) {
            AppendTransformEntryIfDirty(state, transformValues, identity, entity, entries);
        }
        state.lastTransformHierarchyEntryBuildNanoseconds += Nanoseconds(std::chrono::steady_clock::now() - entryBuildStart);
        if (entries.empty()) {
            break;
        }

        ApplyTransformEntries(state, transformValues, entries, updatedEntities);
        state.lastTransformHierarchyDirtyFrontierCount += entries.size();
        nextFrontier.clear();
        const auto frontierAppendStart = std::chrono::steady_clock::now();
        AppendUpdatedChildrenToFrontier(state, entries, nextFrontier);
        state.lastTransformHierarchyFrontierAppendNanoseconds += Nanoseconds(std::chrono::steady_clock::now() - frontierAppendStart);
        currentFrontier.swap(nextFrontier);
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
        if (!hotQuery.Rebuild(queryCache.query, kb::ecs::QueryExecutionSettings{ .maxBatchSize = kTransformBatchGrainSize })) {
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

    if (!rootsOnly && dirtyRows <= kTransformBatchGrainSize * 4U) {
        // A few dirty rows next to hierarchies cost the frontier lanes less than a kernel dispatch.
        // Known leaf destruction may leave a queued handle from an earlier
        // write in this frame. The remaining frontier still contains live work.
        std::erase_if(state.transformDirtyFrontierEntities, [&state](SceneEntity entity) { return !state.world.IsAlive(entity); });
        auto& nativeStorage = const_cast<kb::ecs::NativeArchetypeStorage&>(state.world.NativeStorage());
        if (dirtyRows != 0U) static_cast<void>(hotQuery.ForEachDirtyMutableRange<0>(
            nativeStorage, kTransformBatchGrainSize, state.transformNativeDirtyRangesScratch, true,
            [&state](const kb::ecs::UnsafeHotMutableChunk<TransformComponent>& chunk, std::size_t) {
                const TransformComponent* transforms = chunk.Components<0>();
                for (std::size_t row = 0U; row < chunk.Count(); ++row) {
                    if (transforms[row].worldDirty) {
                        EnqueueSceneTransformDirtyFrontierUnchecked(state, chunk.EntityAt(row));
                    }
                }
            }));
        if (!state.transformDirtyFrontierEntities.empty()) {
            return false;
        }
        dirtyRows = 0U;
    }

    ResetPropagationCursor(state);
    // Rows linked to a parent or children are left to the frontier below; every other dirty row is composed by the
    // root kernel wherever the scene's hierarchies are.
    std::vector<SceneTransformBatchEntry>& linkedRows = state.transformHierarchyEntriesScratch;
    linkedRows.clear();
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
        std::mutex linkedRowsMutex;
        std::mutex sparseUpdatedMutex;
        const kb::ecs::UnsafeHotDirtyRangeDispatchStats dispatchStats = hotQuery.ForEachDirtyMutableRangeParallel<0>(
            nativeStorage,
            kTransformBatchGrainSize,
            *state.transformWorkerPool,
            0U,
            true,
            [&state, rootsOnly, listUpdatedRows, &inspectedCount, &updatedCount, &rootFastPathCount, &linkedRows, &linkedRowsMutex, &sparseUpdatedMutex](
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
                            const std::lock_guard lock{ linkedRowsMutex };
                            linkedRows.push_back(SceneTransformBatchEntry{ .entity = chunk.EntityAt(row), .transform = &transform });
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
        std::ranges::sort(linkedRows, {}, [](const SceneTransformBatchEntry& entry) { return entry.entity.Id(); });
    } else {
        static_cast<void>(hotQuery.ForEachDirtyMutableRange<0>(
            nativeStorage,
            kTransformBatchGrainSize,
            state.transformNativeDirtyRangesScratch,
            true,
            [&state, rootsOnly, listUpdatedRows, &linkedRows](kb::ecs::UnsafeHotMutableChunk<TransformComponent>& chunk, std::size_t dirtyCount) {
                state.lastTransformHierarchyInspectedCount += rootsOnly ? chunk.Count() : 0U;
                static_cast<void>(dirtyCount);
                TransformComponent* transforms = chunk.template Components<0>();
                for (std::size_t row = 0U; row < chunk.Count(); ++row) {
                    TransformComponent& transform = transforms[row];
                    if (!transform.worldDirty) {
                        continue;
                    }
                    if (!rootsOnly && SceneHierarchyCache::HasTransformLink(state, chunk.EntityAt(row))) {
                        linkedRows.push_back(SceneTransformBatchEntry{ .entity = chunk.EntityAt(row), .transform = &transform });
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
    bool linkedRowsLeft = false;
    if (!rootsOnly) {
        // Known leaf destruction may leave a queued handle from an earlier write in this frame, and a row that lost
        // its links after it was queued was composed above. The linked dirty rows are the frontier's live work.
        std::erase_if(state.transformDirtyFrontierEntities, [&state](SceneEntity entity) {
            return !state.world.IsAlive(entity) || !SceneHierarchyCache::HasTransformLink(state, entity);
        });
        SceneTransformLeafBatchUpdater leafValidator{state};
        bool independentLeaves = true;
        for (const SceneTransformBatchEntry& linked : linkedRows) {
            EnqueueSceneTransformDirtyFrontierUnchecked(state, linked.entity);
            if (independentLeaves && !leafValidator.CanUpdate(linked.entity)) independentLeaves = false;
        }
        linkedRowsLeft = !independentLeaves || state.transformDirtyFrontierEntities.size() != linkedRows.size();
        if (!linkedRowsLeft) {
            SceneTransformLeafBatchUpdater leafUpdater{state};
            for (const SceneTransformBatchEntry& linked : linkedRows) {
                const auto entry = leafUpdater.Update(linked.entity, *linked.transform);
                state.lastTransformHierarchyRootFastPathCount += entry.rootFastPath;
                state.lastTransformHierarchyTranslatedParentFastPathCount += entry.translatedParentFastPath;
                state.lastTransformHierarchyUnrotatedParentFastPathCount += entry.unrotatedParentFastPath;
                state.lastTransformHierarchyUnitScaleParentFastPathCount += entry.unitScaleParentFastPath;
                state.lastTransformHierarchyUniformScaleParentFastPathCount += entry.uniformScaleParentFastPath;
                state.lastTransformHierarchyStaticLocalRotationFastPathCount += entry.staticLocalRotationFastPath;
                if (listUpdatedRows) {
                    state.transformHierarchyUpdatedEntitiesScratch.push_back(linked.entity);
                    state.transformHierarchyUpdatedTransformsScratch.push_back(*linked.transform);
                }
                RecordUpdatedTransform(state, linked.entity, linked.transform);
            }
            state.lastTransformHierarchyInspectedCount += linkedRows.size();
            state.lastTransformHierarchyUpdatedCount += linkedRows.size();
            state.lastTransformHierarchyDirtyFrontierCount = linkedRows.size();
        }
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
    if (linkedRowsLeft) {
        // Parents moved with their children, or rows were queued without a dirty write: the generic lanes finish the
        // linked rows from the frontier after the rows composed here.
        return false;
    }
    ClearSceneTransformDirtyFrontier(state);
    PrintRootSyncProfile(state, profileTimings, dirtyRows, updateStart);
    return true;
}

} // namespace

void EnsureSceneTransformWorkerPool(SceneState& state) {
    EnsureWorkerPool(state);
}

// The engine side of a ParallelForEachRoot task: one per worker range of chunks.
struct TransformPassTask {
    struct Deferred {
        SceneEntity entity;
        kb::ecs::NativeComponentRows row;
    };

    TransformPassTask(SceneState& state, std::mutex& sparseMutex) noexcept : state(state), updatedBits(state, sparseMutex) {}

    SceneState& state;
    UpdatedTransformBitWriter updatedBits;
    std::vector<Deferred> deferred;
    std::size_t written = 0U;
    bool trackPrefab = false;
};

class TransformPassAccess {
public:
    static void Bind(TransformRowRange& range, const kb::ecs::MutableQueryTableDispatchRecord& record, std::size_t extraCount,
        TransformPassTask& task, bool composes) noexcept {
        range.entityIds_ = record.entityIds;
        range.rows_ = static_cast<TransformComponent*>(record.fieldComponents[0]);
        range.count_ = record.entityCount;
        for (std::size_t extra = 0U; extra < TransformRowRange::kMaxExtraComponents; ++extra) {
            range.extraColumns_[extra] = extra < extraCount ? record.fieldComponents[extra + 1U] : nullptr;
        }
        range.pass_ = &task;
        range.archetypeIndex_ = record.nativeArchetypeIndex;
        range.chunkIndex_ = record.nativeChunkIndex;
        range.composes_ = composes;
    }

    static void SetLocal(TransformRowRange& range, std::size_t row, const Vec3& position, const Quat& rotation, const Vec3& scale) {
        auto& task = *static_cast<TransformPassTask*>(range.pass_);
        TransformComponent& transform = range.rows_[row];
        const SceneEntity entity{ range.entityIds_[row] };
        transform.localPosition = position;
        transform.localRotation = rotation;
        transform.localScale = scale;
        ++transform.localVersion;
        transform.worldDirty = true;
        ++task.written;
        std::uint32_t nodeIndex = 0U;
        if (!range.composes_ || SceneHierarchyCache::HasTransformLink(task.state, entity) ||
            (task.trackPrefab && task.state.prefabInstances.FindContainingEntity(entity, nodeIndex).IsValid())) {
            task.deferred.push_back({ entity, kb::ecs::NativeComponentRows{ .archetypeIndex = range.archetypeIndex_, .chunkIndex = range.chunkIndex_, .firstRow = row, .count = 1U } });
            return;
        }
        ComposeSceneTransformRoot(transform);
        task.updatedBits.Record(entity, transform);
    }
};

void TransformRowRange::SetLocal(std::size_t row, const Vec3& position, const Quat& rotation, const Vec3& scale) {
    TransformPassAccess::SetLocal(*this, row, position, rotation, scale);
}

TransformPassStats RunSceneTransformPass(SceneState& state, std::size_t grainRows, std::span<const kb::ecs::ComponentId> extraComponents,
    SceneTransforms::TransformRangeBody body, void* context) {
    TransformPassStats stats;
    if (body == nullptr) return stats;
    if (extraComponents.size() > TransformRowRange::kMaxExtraComponents) {
        throw std::invalid_argument("A transform pass takes at most four extra components");
    }
    if (state.transformPassRunning) {
        throw std::logic_error("A transform pass cannot run inside another one");
    }
    const kb::ecs::ComponentId transformId = state.components.TransformComponentId();
    auto& storage = const_cast<kb::ecs::NativeArchetypeStorage&>(state.world.NativeStorage());
    std::array<kb::ecs::ComponentId, 5U> ids{ transformId };
    for (std::size_t extra = 0U; extra < extraComponents.size(); ++extra) {
        if (extraComponents[extra] == 0U) return stats;
        ids[extra + 1U] = extraComponents[extra];
    }
    const std::span<const kb::ecs::ComponentId> queried{ ids.data(), extraComponents.size() + 1U };
    if (state.transformPassRecordsVersion != storage.StructuralVersion() || state.transformPassRecordIds != ids || state.transformPassRecords.empty()) {
        storage.CollectMutableQueryRecords(queried, {}, {}, state.transformPassRecords);
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
    std::mutex sparseMutex;
    std::vector<std::unique_ptr<TransformPassTask>> tasks(taskCount);
    std::atomic_bool stopped{ false };
    std::exception_ptr firstException;
    std::mutex exceptionMutex;
    EnsureWorkerPool(state);
    state.transformPassRunning = true;
    {
        const auto iterationGuard = state.world.EnterIteration();
        try {
            state.transformWorkerPool->ParallelForChunks(records.size(), recordsPerTask, [&](kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk& chunk) {
                tasks[chunk.index] = std::make_unique<TransformPassTask>(state, sparseMutex);
                TransformPassTask& task = *tasks[chunk.index];
                task.trackPrefab = trackPrefab;
                for (std::size_t index = chunk.begin; index < chunk.begin + chunk.count && !stopped.load(std::memory_order_relaxed); ++index) {
                    const auto& record = records[index];
                    const bool composes = !observed && (record.nativeArchetypeIndex >= state.transformPassDeferredArchetypes.size() ||
                        !state.transformPassDeferredArchetypes[record.nativeArchetypeIndex]);
                    TransformRowRange range;
                    TransformPassAccess::Bind(range, record, extraComponents.size(), task, composes);
                    try {
                        body(range, context);
                    } catch (...) {
                        const std::lock_guard lock{ exceptionMutex };
                        if (firstException == nullptr) firstException = std::current_exception();
                        stopped.store(true, std::memory_order_relaxed);
                    }
                }
                task.updatedBits.Finish();
            });
        } catch (...) {
            state.transformPassRunning = false;
            throw;
        }
    }
    state.transformPassRunning = false;

    // Serial publication: the version of each written table once, then the deferred rows as Set flags them.
    std::size_t lastArchetype = std::numeric_limits<std::size_t>::max();
    for (std::size_t index = 0U; index < records.size(); ++index) {
        stats.rowsVisited += records[index].entityCount;
        ++stats.ranges;
    }
    std::vector<kb::ecs::NativeComponentRows>& deferredRows = state.transformSetManyRowsScratch;
    deferredRows.clear();
    for (const auto& task : tasks) {
        if (task == nullptr) continue;
        stats.rowsWritten += task->written;
        stats.rowsDeferred += task->deferred.size();
        for (const auto& deferred : task->deferred) {
            if (!observed) {
                kb::ecs::NativeComponentRows* run = deferredRows.empty() ? nullptr : &deferredRows.back();
                if (run != nullptr && run->archetypeIndex == deferred.row.archetypeIndex && run->chunkIndex == deferred.row.chunkIndex &&
                    run->firstRow + run->count == deferred.row.firstRow) {
                    ++run->count;
                } else {
                    deferredRows.push_back(deferred.row);
                }
            }
        }
    }
    for (const auto& record : records) {
        if (record.nativeArchetypeIndex != lastArchetype) {
            lastArchetype = record.nativeArchetypeIndex;
            if (stats.rowsWritten != 0U) storage.NoteComponentWritten(lastArchetype, transformId);
        }
    }
    if (!deferredRows.empty()) storage.MarkComponentRowsModified(transformId, deferredRows);
    for (const auto& task : tasks) {
        if (task == nullptr) continue;
        for (const auto& deferred : task->deferred) {
            if (observed) state.componentStorage.Transforms().MarkWritten(deferred.entity);
            if (SceneHierarchyCache::HasTransformLink(state, deferred.entity)) EnqueueSceneTransformDirtyFrontier(state, deferred.entity);
            if (trackPrefab) MarkScenePrefabNodeDirty(state, deferred.entity);
        }
    }
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
    SceneComponentIteration::ForEachTransform(state.world, state.components.TransformComponentId(),
        [](SceneEntity entity, const TransformComponent& transform, void* context) {
            SceneState& state = *static_cast<SceneState*>(context);
            const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(entity);
            if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex && denseIndex / 64U < state.transformUpdatedBits.size() &&
                IsTransformUpdated(state, entity)) {
                AppendRenderProxyListEntry(state, entity, transform);
            }
        }, &state);
    for (const SceneEntity entity : state.transformUpdatedSparseEntities) {
        if (const TransformComponent* transform = state.componentStorage.Transforms().TryGet(entity); transform != nullptr) {
            AppendRenderProxyListEntry(state, entity, *transform);
        }
    }
    for (std::size_t proxyIndex = 0U; proxyIndex < state.transformRenderProxyUpdateEntities.size(); ++proxyIndex) {
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
    const auto denseScratchStart = profileTimings != nullptr ? Clock::now() : Clock::time_point{};
    PrepareDenseTransformScratch(state);
    if (profileTimings != nullptr) {
        profileTimings->denseScratchNanoseconds = Nanoseconds(Clock::now() - denseScratchStart);
    }
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

    if (!state.transformDirtyFrontierEntities.empty() && budgetLimit == 0U && state.transformPropagationCursorLevel == 0U && state.transformPropagationCursorOffset == 0U) {
        const auto cacheBuildStart = Clock::now();
        TransformValueCache transformValues = BuildDirtyFrontierTransformValueCache(state);
        const auto cacheBuildEnd = Clock::now();
        state.lastTransformHierarchyCacheBuildNanoseconds = Nanoseconds(cacheBuildEnd - cacheBuildStart);
        kb::ecs::ReserveGeometric(updatedEntities, nativeUpdatedCount + state.transformDirtyFrontierEntities.size());
        if (CanUseHierarchyDirtyFrontier(state, transformValues)) {
            RunHierarchyDirtyFrontier(state, transformValues, identity, entries, updatedEntities);
            ResetPropagationCursor(state);
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
            return;
        }
        entries.clear();
        updatedEntities.resize(nativeUpdatedCount);
    }

    const auto cacheBuildStart = Clock::now();
    TransformValueCache transformValues = BuildTransformValueCache(state);
    const auto cacheBuildEnd = Clock::now();
    state.lastTransformHierarchyCacheBuildNanoseconds = Nanoseconds(cacheBuildEnd - cacheBuildStart);
    PrewarmTransformScratchForCompletedLevels(state, transformValues);

    kb::ecs::ReserveGeometric(updatedEntities, nativeUpdatedCount + trackedSlotCount);
    if (CanUseHierarchyDirtyFrontier(state, transformValues)) {
        RunHierarchyDirtyFrontier(state, transformValues, identity, entries, updatedEntities);
        ResetPropagationCursor(state);
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
        return;
    }

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
