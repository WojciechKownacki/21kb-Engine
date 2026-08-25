#include "scene/transform/SceneTransformHierarchySystem.hpp"

#include "engine/ecs/Query.hpp"
#include "engine/ecs/UnsafeHotQuery.hpp"
#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentRegistry.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"
#include "scene/SceneRenderProxyComponentMask.hpp"
#include "scene/transform/SceneTransformBranchUpdater.hpp"
#include "scene/transform/SceneTransformDirtyFrontier.hpp"
#include "scene/transform/SceneTransformRootHotKernel.hpp"
#include "scene/transform/TransformMath.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <unordered_map>
#include <vector>

namespace kb::scene {
namespace {

inline constexpr std::size_t kTransformBatchGrainSize = 128U;
inline constexpr std::size_t kSparseTransformFlushLookupFactor = 8U;
inline constexpr std::size_t kDirtyListTransformFlushFactor = 2U;

template <typename Duration>
[[nodiscard]] std::uint64_t Nanoseconds(Duration duration) noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
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
    std::atomic_size_t loadedCount = 0U;
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
        buildContext.loadedCount.fetch_add(1U, std::memory_order_relaxed);
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
    buildContext.loadedCount.fetch_add(1U, std::memory_order_relaxed);
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
    settings.maxBatchSize = kTransformBatchGrainSize;
    settings.policy = kb::ecs::QueryExecutionPolicy::SingleThread;
    TransformCacheBuildContext context{ .cache = &cache };
    if (state.hierarchyOrder.size() > kTransformBatchGrainSize * 32U) {
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
        state.transformHierarchyFlushComponentsScratch.reserve(updatedEntities.size());
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
        state.transformHierarchyFlushComponentsScratch.reserve(updatedEntities.size());
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
    if (!state.world.Config().mirrorNativeComponentChangesToBackend) {
        return stats;
    }
    for (const SceneEntity entity : updatedEntities) {
        SceneComponentStorageAccess::MarkModified<TransformComponent>(&state.world, entity);
    }
    stats.backendMarkNanoseconds = Nanoseconds(std::chrono::steady_clock::now() - backendMarkStart);
    return stats;
}

void BuildTopologicalBatches(SceneState& state) {
    if (state.transformTopologicalBatchesVersion == state.hierarchyTopologyVersion) {
        return;
    }

    state.transformTopologicalBatches.clear();
    if (state.hierarchyRoots.empty()) {
        state.transformTopologicalBatchesVersion = state.hierarchyTopologyVersion;
        ++state.transformTopologicalBatchBuildCount;
        return;
    }

    state.transformTopologicalBatches.push_back(state.hierarchyRoots);
    for (std::size_t levelIndex = 0; levelIndex < state.transformTopologicalBatches.size(); ++levelIndex) {
        const std::vector<SceneEntity>& level = state.transformTopologicalBatches[levelIndex];
        std::vector<SceneEntity>* nextLevel = nullptr;
        for (const SceneEntity entity : level) {
            std::span<const SceneEntity> childEntities;
            const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(entity);
            if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex && denseIndex < state.denseHierarchyChildren.size()) {
                childEntities = state.denseHierarchyChildren[denseIndex];
            } else {
                const auto children = state.hierarchyChildren.find(entity.Id());
                if (children != state.hierarchyChildren.end()) {
                    childEntities = children->second;
                }
            }
            if (childEntities.empty()) {
                continue;
            }
            if (nextLevel == nullptr) {
                if (levelIndex + 1U == state.transformTopologicalBatches.size()) {
                    state.transformTopologicalBatches.emplace_back();
                }
                nextLevel = &state.transformTopologicalBatches[levelIndex + 1U];
            }
            nextLevel->insert(nextLevel->end(), childEntities.begin(), childEntities.end());
        }
    }
    state.transformTopologicalBatchesVersion = state.hierarchyTopologyVersion;
    ++state.transformTopologicalBatchBuildCount;
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

[[nodiscard]] WorldTransformAffine3x4 BuildWorldAffine3x4(const TransformComponent& transform) noexcept {
    if (transform.worldRotation.x == 0.0F &&
        transform.worldRotation.y == 0.0F &&
        transform.worldRotation.z == 0.0F &&
        transform.worldRotation.w == 1.0F) {
        WorldTransformAffine3x4 affine;
        affine.values[0] = transform.worldScale.x;
        affine.values[1] = 0.0F;
        affine.values[2] = 0.0F;
        affine.values[3] = 0.0F;
        affine.values[4] = transform.worldScale.y;
        affine.values[5] = 0.0F;
        affine.values[6] = 0.0F;
        affine.values[7] = 0.0F;
        affine.values[8] = transform.worldScale.z;
        affine.values[9] = transform.worldPosition.x;
        affine.values[10] = transform.worldPosition.y;
        affine.values[11] = transform.worldPosition.z;
        return affine;
    }

    const float x = transform.worldRotation.x;
    const float y = transform.worldRotation.y;
    const float z = transform.worldRotation.z;
    const float w = transform.worldRotation.w;
    const float xx = x * x;
    const float yy = y * y;
    const float zz = z * z;
    const float xy = x * y;
    const float xz = x * z;
    const float yz = y * z;
    const float wx = w * x;
    const float wy = w * y;
    const float wz = w * z;
    const float sx = transform.worldScale.x;
    const float sy = transform.worldScale.y;
    const float sz = transform.worldScale.z;

    WorldTransformAffine3x4 affine;
    affine.values[0] = (1.0F - 2.0F * (yy + zz)) * sx;
    affine.values[1] = (2.0F * (xy + wz)) * sx;
    affine.values[2] = (2.0F * (xz - wy)) * sx;
    affine.values[3] = (2.0F * (xy - wz)) * sy;
    affine.values[4] = (1.0F - 2.0F * (xx + zz)) * sy;
    affine.values[5] = (2.0F * (yz + wx)) * sy;
    affine.values[6] = (2.0F * (xz + wy)) * sz;
    affine.values[7] = (2.0F * (yz - wx)) * sz;
    affine.values[8] = (1.0F - 2.0F * (xx + yy)) * sz;
    affine.values[9] = transform.worldPosition.x;
    affine.values[10] = transform.worldPosition.y;
    affine.values[11] = transform.worldPosition.z;
    return affine;
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
    if (state.transformWorkerPool == nullptr) {
        state.transformWorkerPool = std::make_unique<kb::ecs::WorkerPool>(kb::ecs::WorkerPoolConfig{});
    } else if (!state.transformWorkerPool->Running()) {
        state.transformWorkerPool->Start(kb::ecs::WorkerPoolConfig{});
    }
}

template <typename TransformResolver>
void CacheRenderProxyUpdatesAfterTransformsWithResolver(
    SceneState& state,
    std::span<const SceneEntity> updatedEntities,
    TransformResolver resolveTransform) {
    if (updatedEntities.empty()) {
        return;
    }

    const std::size_t writeBegin = state.transformRenderProxyUpdateEntities.size();
    state.transformRenderProxyUpdateEntities.resize(writeBegin + updatedEntities.size());
    state.transformRenderProxyWorldAffine3x4.resize(writeBegin + updatedEntities.size());
    const auto writeProxy = [&state, updatedEntities, writeBegin, &resolveTransform](std::size_t offset) -> bool {
        const SceneEntity entity = updatedEntities[offset];
        state.transformRenderProxyUpdateEntities[writeBegin + offset] = entity;
        if (const TransformComponent* transform = resolveTransform(entity, offset); transform != nullptr) {
            WorldTransformAffine3x4& affine = state.transformRenderProxyWorldAffine3x4[writeBegin + offset];
            if (SceneTransformRootHotKernel::CanWriteIdentityAffineFastPath(*transform)) {
                SceneTransformRootHotKernel::WriteIdentityAffine(*transform, affine);
                return true;
            }
            affine = BuildWorldAffine3x4(*transform);
        } else {
            state.transformRenderProxyWorldAffine3x4[writeBegin + offset] = WorldTransformAffine3x4{};
        }
        return false;
    };
    if (updatedEntities.size() <= kTransformBatchGrainSize) {
        std::size_t identityAffineFastPathCount = 0U;
        for (std::size_t offset = 0; offset < updatedEntities.size(); ++offset) {
            identityAffineFastPathCount += writeProxy(offset) ? 1U : 0U;
        }
        state.lastTransformRenderProxyIdentityAffineFastPathCount += identityAffineFastPathCount;
    } else {
        EnsureWorkerPool(state);
        const std::size_t chunkCount = (updatedEntities.size() + kTransformBatchGrainSize - 1U) / kTransformBatchGrainSize;
        state.transformRenderProxyIdentityAffineChunkCountsScratch.clear();
        state.transformRenderProxyIdentityAffineChunkCountsScratch.resize(chunkCount);
        state.transformWorkerPool->ParallelForChunks(updatedEntities.size(), kTransformBatchGrainSize, [&state, &writeProxy](kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk& chunk) {
            std::size_t identityAffineFastPathCount = 0U;
            for (std::size_t offset = 0; offset < chunk.count; ++offset) {
                identityAffineFastPathCount += writeProxy(chunk.begin + offset) ? 1U : 0U;
            }
            state.transformRenderProxyIdentityAffineChunkCountsScratch[chunk.index] = identityAffineFastPathCount;
        });
        for (const std::size_t identityAffineFastPathCount : state.transformRenderProxyIdentityAffineChunkCountsScratch) {
            state.lastTransformRenderProxyIdentityAffineFastPathCount += identityAffineFastPathCount;
        }
    }

    state.transformRenderProxyMeshRendererIndices.reserve(state.transformRenderProxyMeshRendererIndices.size() + updatedEntities.size());
    state.transformRenderProxyVisibleMeshRendererIndices.reserve(state.transformRenderProxyVisibleMeshRendererIndices.size() + updatedEntities.size());
    state.transformRenderProxyCameraIndices.reserve(state.transformRenderProxyCameraIndices.size() + updatedEntities.size());
    state.transformRenderProxyLightIndices.reserve(state.transformRenderProxyLightIndices.size() + updatedEntities.size());
    for (std::size_t offset = 0; offset < updatedEntities.size(); ++offset) {
        const std::size_t proxyIndex = writeBegin + offset;
        const SceneEntity entity = updatedEntities[offset];
        const std::uint8_t componentMask = SceneRenderProxyComponentMaskOf(state, entity);
        if (SceneRenderProxyMaskHas(componentMask, SceneRenderProxyComponentMask::MeshRenderer)) {
            state.transformRenderProxyMeshRendererIndices.push_back(proxyIndex);
            if (!SceneRenderProxyMaskHas(componentMask, SceneRenderProxyComponentMask::Hidden)) {
                state.transformRenderProxyVisibleMeshRendererIndices.push_back(proxyIndex);
            }
        }
        if (SceneRenderProxyMaskHas(componentMask, SceneRenderProxyComponentMask::Camera)) {
            state.transformRenderProxyCameraIndices.push_back(proxyIndex);
            MarkSceneRenderProxyDirty(state, entity);
        }
        if (SceneRenderProxyMaskHas(componentMask, SceneRenderProxyComponentMask::Light)) {
            state.transformRenderProxyLightIndices.push_back(proxyIndex);
            MarkSceneRenderProxyDirty(state, entity);
        }
    }
}

void CacheRenderProxyUpdatesAfterTransforms(
    SceneState& state,
    std::span<const SceneEntity> updatedEntities,
    std::span<const TransformComponent> updatedTransforms) {
    CacheRenderProxyUpdatesAfterTransformsWithResolver(
        state,
        updatedEntities,
        [updatedTransforms](SceneEntity, std::size_t offset) -> const TransformComponent* {
            if (offset < updatedTransforms.size()) {
                return &updatedTransforms[offset];
            }
            return nullptr;
        });
}

void CacheRenderProxyUpdatesAfterTransforms(
    SceneState& state,
    const TransformValueCache& transformValues,
    std::span<const SceneEntity> updatedEntities) {
    CacheRenderProxyUpdatesAfterTransformsWithResolver(
        state,
        updatedEntities,
        [&transformValues](SceneEntity entity, std::size_t) -> const TransformComponent* {
            return transformValues.Find(entity);
        });
}

void ResetPropagationCursor(SceneState& state) noexcept {
    state.transformPropagationCursorVersion = state.hierarchyTopologyVersion;
    state.transformPropagationCursorLevel = 0U;
    state.transformPropagationCursorOffset = 0U;
}

void PrewarmTransformScratchForCompletedLevels(SceneState& state, const TransformValueCache& transformValues) {
    const std::size_t levelCount = std::min(state.transformPropagationCursorLevel, state.transformTopologicalBatches.size());
    for (std::size_t levelIndex = 0; levelIndex < levelCount; ++levelIndex) {
        for (const SceneEntity entity : state.transformTopologicalBatches[levelIndex]) {
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
    state.transformValueCacheLoadEntitiesScratch.reserve(std::min<std::size_t>(
        state.hierarchyOrder.size(),
        std::max<std::size_t>(state.transformDirtyFrontierEntities.size() * 4U, 16U)));

    std::vector<SceneEntity>& subtreeStack = state.transformDirtyFrontierLevelScratch;
    subtreeStack.clear();
    subtreeStack.reserve(state.transformDirtyFrontierEntities.size());
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
    currentFrontier.reserve(state.transformDirtyFrontierEntities.size());
    for (const SceneEntity entity : state.transformDirtyFrontierEntities) {
        if (HasDirtyAncestorInFrontier(state, entity)) {
            continue;
        }
        currentFrontier.push_back(entity);
    }

    while (!currentFrontier.empty()) {
        entries.clear();
        entries.reserve(currentFrontier.size());
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
        && state.transformTopologicalBatches.size() == 1U
        && !state.transformTopologicalBatches.front().empty();
}

[[nodiscard]] bool RunNativeRootOnlyDirtyRanges(SceneState& state, std::chrono::steady_clock::time_point updateStart) {
    using Clock = std::chrono::steady_clock;
    if (!CanUseNativeRootOnlyDirtyRanges(state)) {
        return false;
    }

    kb::ecs::Query<TransformComponent> query = state.world.CreateQuery<TransformComponent>();
    if (!query.IsValid()) {
        return false;
    }

    kb::ecs::UnsafeHotQuery<TransformComponent> hotQuery;
    if (!hotQuery.Rebuild(query, kb::ecs::QueryExecutionSettings{ .maxBatchSize = kTransformBatchGrainSize })) {
        return false;
    }

    std::size_t dirtyRows = 0U;
    hotQuery.ForEachMutableChunk([&dirtyRows](const kb::ecs::UnsafeHotMutableChunk<TransformComponent>& chunk) {
        dirtyRows += chunk.DirtyCount<0>();
    });

    ResetPropagationCursor(state);
    state.transformHierarchyUpdatedEntitiesScratch.clear();
    state.transformHierarchyUpdatedTransformsScratch.clear();
    if (dirtyRows == 0U) {
        const auto finishedAt = Clock::now();
        state.lastTransformHierarchyUpdateNanoseconds = Nanoseconds(finishedAt - updateStart);
        state.lastTransformHierarchyPropagateNanoseconds = state.lastTransformHierarchyUpdateNanoseconds;
        return true;
    }

    state.transformHierarchyUpdatedEntitiesScratch.reserve(dirtyRows);
    state.transformHierarchyUpdatedTransformsScratch.reserve(dirtyRows);
    auto& nativeStorage = const_cast<kb::ecs::NativeArchetypeStorage&>(state.world.NativeStorage());
    const auto applyStart = Clock::now();
    if (dirtyRows > kTransformBatchGrainSize * 4U) {
        EnsureWorkerPool(state);
        state.transformHierarchyUpdatedEntitiesScratch.resize(dirtyRows);
        state.transformHierarchyUpdatedTransformsScratch.resize(dirtyRows);
        std::atomic_size_t inspectedCount{ 0U };
        std::atomic_size_t updatedCount{ 0U };
        std::atomic_size_t rootFastPathCount{ 0U };
        const kb::ecs::UnsafeHotDirtyRangeDispatchStats dispatchStats = hotQuery.ForEachDirtyMutableRangeParallel<0>(
            nativeStorage,
            kTransformBatchGrainSize,
            *state.transformWorkerPool,
            0U,
            true,
            [&state, &inspectedCount, &updatedCount, &rootFastPathCount](
                kb::ecs::UnsafeHotMutableChunk<TransformComponent>& chunk,
                std::size_t dirtyCount,
                kb::ecs::WorkerContext workerContext) {
                static_cast<void>(workerContext);
                static_cast<void>(dirtyCount);
                inspectedCount.fetch_add(chunk.Count(), std::memory_order_relaxed);
                std::size_t localUpdatedCount = 0U;
                std::size_t localRootFastPathCount = 0U;
                TransformComponent* transforms = chunk.template Components<0>();
                for (std::size_t row = 0U; row < chunk.Count(); ++row) {
                    localUpdatedCount += transforms[row].worldDirty ? 1U : 0U;
                }
                if (localUpdatedCount == 0U) {
                    return;
                }

                const std::size_t writeBegin = updatedCount.fetch_add(localUpdatedCount, std::memory_order_relaxed);
                rootFastPathCount.fetch_add(localUpdatedCount, std::memory_order_relaxed);
                std::size_t writeOffset = 0U;
                for (std::size_t row = 0U; row < chunk.Count(); ++row) {
                    TransformComponent& transform = transforms[row];
                    if (transform.worldDirty) {
                        if (SceneTransformRootHotKernel::CanApplyIdentityRotationFastPath(transform)) {
                            SceneTransformRootHotKernel::ApplyIdentityRotationRoot(transform);
                            ++localRootFastPathCount;
                        } else {
                            transform = TransformMath::ComposeRoot(transform);
                        }
                        state.transformHierarchyUpdatedEntitiesScratch[writeBegin + writeOffset] = chunk.EntityAt(row);
                        state.transformHierarchyUpdatedTransformsScratch[writeBegin + writeOffset] = transform;
                        ++writeOffset;
                    }
                }
                rootFastPathCount.fetch_add(localRootFastPathCount, std::memory_order_relaxed);
            });
        state.lastTransformHierarchyInspectedCount += inspectedCount.load(std::memory_order_relaxed);
        const std::size_t finalUpdatedCount = updatedCount.load(std::memory_order_relaxed);
        state.transformHierarchyUpdatedEntitiesScratch.resize(finalUpdatedCount);
        state.transformHierarchyUpdatedTransformsScratch.resize(finalUpdatedCount);
        state.lastTransformHierarchyUpdatedCount += finalUpdatedCount;
        state.lastTransformHierarchyRootFastPathCount += rootFastPathCount.load(std::memory_order_relaxed);
        ++state.lastTransformHierarchyParallelBatchCount;
        state.lastTransformHierarchyParallelChunkCount += dispatchStats.ranges;
        state.lastTransformHierarchyParallelEntityCount += dispatchStats.entities;
        state.lastTransformHierarchyWorkerCount = std::max(state.lastTransformHierarchyWorkerCount, state.transformWorkerPool->WorkerCount());
    } else {
        static_cast<void>(hotQuery.ForEachDirtyMutableRange<0>(
            nativeStorage,
            kTransformBatchGrainSize,
            state.transformNativeDirtyRangesScratch,
            true,
            [&state](kb::ecs::UnsafeHotMutableChunk<TransformComponent>& chunk, std::size_t dirtyCount) {
                state.lastTransformHierarchyInspectedCount += chunk.Count();
                static_cast<void>(dirtyCount);
                TransformComponent* transforms = chunk.template Components<0>();
                for (std::size_t row = 0U; row < chunk.Count(); ++row) {
                    TransformComponent& transform = transforms[row];
                    if (!transform.worldDirty) {
                        continue;
                    }
                    if (SceneTransformRootHotKernel::CanApplyIdentityRotationFastPath(transform)) {
                        SceneTransformRootHotKernel::ApplyIdentityRotationRoot(transform);
                        ++state.lastTransformHierarchyRootFastPathCount;
                    } else {
                        transform = TransformMath::ComposeRoot(transform);
                    }
                    ++state.lastTransformHierarchyUpdatedCount;
                    state.transformHierarchyUpdatedEntitiesScratch.push_back(chunk.EntityAt(row));
                    state.transformHierarchyUpdatedTransformsScratch.push_back(transform);
                }
            }));
    }
    const auto applyEnd = Clock::now();
    state.lastTransformHierarchyKernelApplyNanoseconds = Nanoseconds(applyEnd - applyStart);
    state.lastTransformHierarchyUpdateNanoseconds = Nanoseconds(applyEnd - updateStart);
    state.lastTransformHierarchyPropagateNanoseconds = state.lastTransformHierarchyUpdateNanoseconds;
    state.lastTransformHierarchyFlushWriteNanoseconds = state.lastTransformHierarchyKernelApplyNanoseconds;
    state.lastTransformHierarchyFlushedEntityCount = state.transformHierarchyUpdatedEntitiesScratch.size();

    const auto backendMarkStart = Clock::now();
    if (state.world.Config().mirrorNativeComponentChangesToBackend) {
        const std::size_t updatedTransformCount = std::min(
            state.transformHierarchyUpdatedEntitiesScratch.size(),
            state.transformHierarchyUpdatedTransformsScratch.size());
        for (std::size_t index = 0U; index < updatedTransformCount; ++index) {
            SceneComponentAccess::Set(
                state.world.NativeHandle(),
                state.transformHierarchyUpdatedEntitiesScratch[index],
                state.components.TransformComponentId(),
                sizeof(TransformComponent),
                &state.transformHierarchyUpdatedTransformsScratch[index]);
        }
    }
    state.lastTransformHierarchyBackendMarkNanoseconds = Nanoseconds(Clock::now() - backendMarkStart);
    CacheRenderProxyUpdatesAfterTransforms(
        state,
        state.transformHierarchyUpdatedEntitiesScratch,
        state.transformHierarchyUpdatedTransformsScratch);
    ClearSceneTransformDirtyFrontier(state);
    return true;
}

} // namespace

void SceneTransformHierarchySystem::Update(SceneState& state) const {
    using Clock = std::chrono::steady_clock;

    const auto updateStart = Clock::now();
    const TransformComponent identity = TransformMath::Identity();
    BuildTopologicalBatches(state);
    if (state.transformPropagationCursorVersion != state.hierarchyTopologyVersion ||
        state.transformPropagationCursorLevel >= state.transformTopologicalBatches.size()) {
        ResetPropagationCursor(state);
    }

    state.transformDirtyScratch.clear();
    state.transformWorldScratch.clear();
    PrepareDenseTransformScratch(state);
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
    if (RunNativeRootOnlyDirtyRanges(state, updateStart)) {
        return;
    }

    std::vector<SceneTransformBatchEntry>& entries = state.transformHierarchyEntriesScratch;
    std::vector<SceneEntity>& updatedEntities = state.transformHierarchyUpdatedEntitiesScratch;
    entries.clear();
    updatedEntities.clear();
    const std::size_t budgetLimit = state.transformPropagationBudget.maxInspectedEntitiesPerSync;

    if (!state.transformDirtyFrontierEntities.empty() && budgetLimit == 0U && state.transformPropagationCursorLevel == 0U && state.transformPropagationCursorOffset == 0U) {
        const auto cacheBuildStart = Clock::now();
        TransformValueCache transformValues = BuildDirtyFrontierTransformValueCache(state);
        const auto cacheBuildEnd = Clock::now();
        state.lastTransformHierarchyCacheBuildNanoseconds = Nanoseconds(cacheBuildEnd - cacheBuildStart);
        updatedEntities.reserve(state.transformDirtyFrontierEntities.size());
        if (CanUseHierarchyDirtyFrontier(state, transformValues)) {
            RunHierarchyDirtyFrontier(state, transformValues, identity, entries, updatedEntities);
            ResetPropagationCursor(state);
            const auto flushStart = Clock::now();
            state.lastTransformHierarchyUpdateNanoseconds = Nanoseconds(flushStart - updateStart);
            state.lastTransformHierarchyPropagateNanoseconds = Nanoseconds(flushStart - cacheBuildEnd);
            const TransformFlushStats flushStats = FlushDirtyTransforms(state, transformValues, updatedEntities);
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
            CacheRenderProxyUpdatesAfterTransforms(state, transformValues, updatedEntities);
            ClearSceneTransformDirtyFrontier(state);
            return;
        }
        entries.clear();
        updatedEntities.clear();
    }

    const auto cacheBuildStart = Clock::now();
    TransformValueCache transformValues = BuildTransformValueCache(state);
    const auto cacheBuildEnd = Clock::now();
    state.lastTransformHierarchyCacheBuildNanoseconds = Nanoseconds(cacheBuildEnd - cacheBuildStart);
    PrewarmTransformScratchForCompletedLevels(state, transformValues);

    updatedEntities.reserve(trackedSlotCount);
    if (CanUseHierarchyDirtyFrontier(state, transformValues)) {
        RunHierarchyDirtyFrontier(state, transformValues, identity, entries, updatedEntities);
        ResetPropagationCursor(state);
        const auto flushStart = Clock::now();
        state.lastTransformHierarchyUpdateNanoseconds = Nanoseconds(flushStart - updateStart);
        state.lastTransformHierarchyPropagateNanoseconds = Nanoseconds(flushStart - cacheBuildEnd);
        const TransformFlushStats flushStats = FlushDirtyTransforms(state, transformValues, updatedEntities);
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
        CacheRenderProxyUpdatesAfterTransforms(state, transformValues, updatedEntities);
        ClearSceneTransformDirtyFrontier(state);
        return;
    }

    std::size_t remainingBudget = budgetLimit;
    bool completed = true;
    for (std::size_t levelIndex = state.transformPropagationCursorLevel; levelIndex < state.transformTopologicalBatches.size(); ++levelIndex) {
        const std::vector<SceneEntity>& level = state.transformTopologicalBatches[levelIndex];
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
        entries.reserve(levelSlice.size());

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
    const TransformFlushStats flushStats = FlushDirtyTransforms(state, transformValues, updatedEntities);
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
    CacheRenderProxyUpdatesAfterTransforms(state, transformValues, updatedEntities);
    ClearSceneTransformDirtyFrontier(state);
}

} // namespace kb::scene
