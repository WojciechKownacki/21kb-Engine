#include "scene/transform/SceneTransformHierarchySystem.hpp"

#include "engine/ecs/Query.hpp"
#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentRegistry.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"
#include "scene/transform/SceneTransformBranchUpdater.hpp"
#include "scene/transform/TransformMath.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
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
    std::atomic_size_t dispatchedChunkCount = 0U;
    std::atomic_size_t dispatchedEntityCount = 0U;
};

struct TransformCacheBuildContext {
    TransformValueCache* cache = nullptr;
    std::atomic_size_t liveCount = 0U;
    std::mutex sparseMutex;
};

void EnsureWorkerPool(SceneState& state);

[[nodiscard]] TransformValueCache BuildTransformValueCache(SceneState& state) {
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
    query.ForEachBatch(settings, [](const kb::ecs::QueryBatch<TransformComponent>& batch, void* context) {
        const auto* transforms = batch.Components<0>();
        auto* buildContext = static_cast<TransformCacheBuildContext*>(context);
        TransformValueCache& transformCache = *buildContext->cache;
        std::size_t localLiveCount = 0U;
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
                ++localLiveCount;
                continue;
            }

            std::lock_guard lock{ buildContext->sparseMutex };
            transformCache.sparse[entity.Id()] = SceneTransformValueCacheEntry{
                .entity = entity,
                .transform = transforms[row],
                .cacheVersion = transformCache.buildVersion,
                .valid = true,
                .dirty = false,
            };
            ++localLiveCount;
        }
        buildContext->liveCount.fetch_add(localLiveCount, std::memory_order_relaxed);
    }, &context);
    cache.liveCount = context.liveCount.load(std::memory_order_relaxed);
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
        const bool allTrackedDirty = updatedEntities.size() >= transformValues.TrackedCount();
        if (allTrackedDirty) {
            query.ForEachMutableBatch(settings, [](kb::ecs::MutableQueryBatch<TransformComponent>& batch, void* context) {
                auto* transforms = batch.Components<0>();
                auto* flushContext = static_cast<TransformFlushContext*>(context);
                const TransformValueCache& transformCache = *flushContext->transformValues;
                flushContext->dispatchedChunkCount.fetch_add(1U, std::memory_order_relaxed);
                flushContext->dispatchedEntityCount.fetch_add(batch.Count(), std::memory_order_relaxed);
                std::size_t flushedInBatch = 0U;
                for (std::size_t row = 0; row < batch.Count(); ++row) {
                    if (const TransformComponent* cached = transformCache.Find(batch.EntityAt(row)); cached != nullptr) {
                        transforms[row] = *cached;
                        ++flushedInBatch;
                    }
                }
                flushContext->flushedEntityCount.fetch_add(flushedInBatch, std::memory_order_relaxed);
            }, &context);
        } else {
            query.ForEachMutableBatch(settings, [](kb::ecs::MutableQueryBatch<TransformComponent>& batch, void* context) {
                auto* transforms = batch.Components<0>();
                auto* flushContext = static_cast<TransformFlushContext*>(context);
                const TransformValueCache& transformCache = *flushContext->transformValues;
                flushContext->dispatchedChunkCount.fetch_add(1U, std::memory_order_relaxed);
                flushContext->dispatchedEntityCount.fetch_add(batch.Count(), std::memory_order_relaxed);
                std::size_t flushedInBatch = 0U;
                for (std::size_t row = 0; row < batch.Count(); ++row) {
                    if (const TransformComponent* cached = transformCache.FindDirty(batch.EntityAt(row)); cached != nullptr) {
                        transforms[row] = *cached;
                        ++flushedInBatch;
            }
        }
                flushContext->flushedEntityCount.fetch_add(flushedInBatch, std::memory_order_relaxed);
            }, &context);
        }
        stats.flushedEntityCount = context.flushedEntityCount.load(std::memory_order_relaxed);
        stats.parallelFlushCount = 1U;
        stats.parallelFlushChunkCount = context.dispatchedChunkCount.load(std::memory_order_relaxed);
        stats.parallelFlushEntityCount = context.dispatchedEntityCount.load(std::memory_order_relaxed);
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
        && state.denseTransformWorldScratchValid[denseIndex] != 0U) {
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
        && state.denseTransformWorldScratchValid[denseIndex] != 0U) {
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
        && denseIndex < state.denseTransformWorldScratchValid.size() && state.denseTransformWorldScratchValid[denseIndex] != 0U) {
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

void AppendUniqueEntity(std::vector<SceneEntity>& entities, SceneEntity entity) {
    if (std::ranges::find(entities, entity) == entities.end()) {
        entities.push_back(entity);
    }
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
        state.denseTransformWorldScratchValid[denseIndex] = 1U;
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

void CacheRenderProxyUpdatesAfterTransforms(SceneState& state, std::span<const SceneEntity> updatedEntities) {
    if (updatedEntities.empty()) {
        return;
    }

    const std::size_t writeBegin = state.transformRenderProxyUpdateEntities.size();
    state.transformRenderProxyUpdateEntities.resize(writeBegin + updatedEntities.size());
    if (updatedEntities.size() <= kTransformBatchGrainSize) {
        std::ranges::copy(updatedEntities, state.transformRenderProxyUpdateEntities.begin() + static_cast<std::ptrdiff_t>(writeBegin));
        return;
    }

    EnsureWorkerPool(state);
    state.transformWorkerPool->ParallelForChunks(updatedEntities.size(), kTransformBatchGrainSize, [&state, updatedEntities, writeBegin](kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk& chunk) {
        for (std::size_t offset = 0; offset < chunk.count; ++offset) {
            state.transformRenderProxyUpdateEntities[writeBegin + chunk.begin + offset] = updatedEntities[chunk.begin + offset];
        }
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

[[nodiscard]] bool CanUseRootHierarchyDirtyFrontier(const SceneState& state, const TransformValueCache& transformValues) noexcept {
    if (state.transformDirtyFrontierEntities.empty() || state.transformPropagationBudget.maxInspectedEntitiesPerSync > 0U
        || state.transformPropagationCursorLevel != 0U || state.transformPropagationCursorOffset != 0U) {
        return false;
    }
    for (const SceneEntity entity : state.transformDirtyFrontierEntities) {
        if (!entity.IsValid() || ParentOf(state, entity).IsValid() || transformValues.Find(entity) == nullptr) {
            return false;
        }
    }
    return true;
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
                state.denseTransformWorldScratchValid[denseIndex] = 1U;
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

void RunRootHierarchyDirtyFrontier(
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
        AppendUniqueEntity(currentFrontier, entity);
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
    state.denseTransformWorldScratch.resize(state.denseHierarchyParents.size());
    state.denseTransformWorldScratchValid.assign(state.denseTransformWorldScratch.size(), 0U);
    state.denseTransformDirtyScratch.assign(state.denseTransformWorldScratch.size(), 0U);
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
    const auto cacheBuildStart = Clock::now();
    TransformValueCache transformValues = BuildTransformValueCache(state);
    const auto cacheBuildEnd = Clock::now();
    state.lastTransformHierarchyCacheBuildNanoseconds = Nanoseconds(cacheBuildEnd - cacheBuildStart);
    PrewarmTransformScratchForCompletedLevels(state, transformValues);

    std::vector<SceneTransformBatchEntry>& entries = state.transformHierarchyEntriesScratch;
    std::vector<SceneEntity>& updatedEntities = state.transformHierarchyUpdatedEntitiesScratch;
    entries.clear();
    updatedEntities.clear();
    updatedEntities.reserve(trackedSlotCount);
    const std::size_t budgetLimit = state.transformPropagationBudget.maxInspectedEntitiesPerSync;
    if (CanUseRootHierarchyDirtyFrontier(state, transformValues)) {
        RunRootHierarchyDirtyFrontier(state, transformValues, identity, entries, updatedEntities);
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
        CacheRenderProxyUpdatesAfterTransforms(state, updatedEntities);
        state.transformDirtyFrontierEntities.clear();
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
    CacheRenderProxyUpdatesAfterTransforms(state, updatedEntities);
    state.transformDirtyFrontierEntities.clear();
}

} // namespace kb::scene
