#pragma once

#include "engine/ecs/NativeArchetypeStorage.hpp"
#include "engine/ecs/Query.hpp"
#include "engine/ecs/QueryExecutionScratch.hpp"
#include "engine/ecs/QueryExecutionTuning.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace kb::ecs {

struct UnsafeHotDirtyRangeDispatchStats {
    std::size_t ranges = 0U;
    std::size_t chunks = 0U;
    std::size_t entities = 0U;
    std::size_t dirtyRows = 0U;
    std::size_t maxRangeSize = 0U;
    std::size_t requestedRangeSize = 0U;
    std::size_t workerCountLimit = 0U;
    std::size_t bytesRead = 0U;
    std::size_t bytesWritten = 0U;
    std::size_t bytesTouched = 0U;
    double bytesPerEntity = 0.0;
};

struct UnsafeHotRangeDispatchStats {
    std::size_t ranges = 0U;
    std::size_t chunks = 0U;
    std::size_t entities = 0U;
    std::size_t maxRangeSize = 0U;
    std::size_t requestedRangeSize = 0U;
    std::size_t cachedRangeSize = 0U;
    std::size_t workerCountLimit = 0U;
    std::size_t bytesRead = 0U;
    std::size_t bytesWritten = 0U;
    std::size_t bytesTouched = 0U;
    double bytesPerEntity = 0.0;
};

template <typename... ComponentTypes>
class UnsafeHotChunk {
public:
    using ComponentTuple = std::tuple<ComponentTypes...>;
    using ComponentPointers = std::array<const void*, sizeof...(ComponentTypes)>;
    using ComponentDirtyCounts = std::array<std::size_t, sizeof...(ComponentTypes)>;

    UnsafeHotChunk(
        const Entity::IdType* entityIds,
        std::size_t count,
        ComponentPointers components,
        std::size_t rangeIndex = 0U,
        ComponentDirtyCounts componentDirtyCounts = {}) noexcept
        : entityIds_(entityIds)
        , count_(count)
        , components_(components)
        , rangeIndex_(rangeIndex)
        , componentDirtyCounts_(componentDirtyCounts) {}

    [[nodiscard]] std::size_t Count() const noexcept {
        return count_;
    }

    [[nodiscard]] bool Empty() const noexcept {
        return count_ == 0U;
    }

    [[nodiscard]] Entity EntityAt(std::size_t index) const noexcept {
        return index < count_ ? Entity{ entityIds_[index] } : Entity{};
    }

    [[nodiscard]] std::size_t RangeIndex() const noexcept {
        return rangeIndex_;
    }

    template <std::size_t Index>
    [[nodiscard]] const auto* Components() const noexcept {
        static_assert(Index < sizeof...(ComponentTypes), "ECS unsafe hot query component index is out of range");
        using Component = std::tuple_element_t<Index, ComponentTuple>;
        return static_cast<const Component*>(components_[Index]);
    }

    template <std::size_t Index>
    [[nodiscard]] std::size_t DirtyCount() const noexcept {
        static_assert(Index < sizeof...(ComponentTypes), "ECS unsafe hot query component index is out of range");
        return componentDirtyCounts_[Index];
    }

private:
    const Entity::IdType* entityIds_ = nullptr;
    std::size_t count_ = 0U;
    ComponentPointers components_{};
    std::size_t rangeIndex_ = 0U;
    ComponentDirtyCounts componentDirtyCounts_{};
};

template <typename... ComponentTypes>
class UnsafeHotMutableChunk {
public:
    using ComponentTuple = std::tuple<ComponentTypes...>;
    using ComponentPointers = std::array<void*, sizeof...(ComponentTypes)>;
    using ComponentDirtyCounts = std::array<std::size_t, sizeof...(ComponentTypes)>;

    UnsafeHotMutableChunk(
        const Entity::IdType* entityIds,
        std::size_t count,
        ComponentPointers components,
        std::size_t rangeIndex = 0U,
        ComponentDirtyCounts componentDirtyCounts = {}) noexcept
        : entityIds_(entityIds)
        , count_(count)
        , components_(components)
        , rangeIndex_(rangeIndex)
        , componentDirtyCounts_(componentDirtyCounts) {}

    [[nodiscard]] std::size_t Count() const noexcept {
        return count_;
    }

    [[nodiscard]] bool Empty() const noexcept {
        return count_ == 0U;
    }

    [[nodiscard]] Entity EntityAt(std::size_t index) const noexcept {
        return index < count_ ? Entity{ entityIds_[index] } : Entity{};
    }

    [[nodiscard]] std::size_t RangeIndex() const noexcept {
        return rangeIndex_;
    }

    template <std::size_t Index>
    [[nodiscard]] auto* Components() const noexcept {
        static_assert(Index < sizeof...(ComponentTypes), "ECS unsafe hot query component index is out of range");
        using Component = std::tuple_element_t<Index, ComponentTuple>;
        return static_cast<Component*>(components_[Index]);
    }

    template <std::size_t Index>
    [[nodiscard]] std::size_t DirtyCount() const noexcept {
        static_assert(Index < sizeof...(ComponentTypes), "ECS unsafe hot query component index is out of range");
        return componentDirtyCounts_[Index];
    }

private:
    const Entity::IdType* entityIds_ = nullptr;
    std::size_t count_ = 0U;
    ComponentPointers components_{};
    std::size_t rangeIndex_ = 0U;
    ComponentDirtyCounts componentDirtyCounts_{};
};

template <typename... ComponentTypes>
class UnsafeHotReadQuery {
public:
    using Chunk = UnsafeHotChunk<ComponentTypes...>;
    using ComponentIdArray = std::array<ComponentId, sizeof...(ComponentTypes)>;

    static_assert(sizeof...(ComponentTypes) > 0, "ECS unsafe hot query must have at least one component");
    static_assert(sizeof...(ComponentTypes) <= kQueryExecutionScratchMaxTerms, "ECS unsafe hot query exceeds the max query term count");
    static_assert((std::is_trivially_copyable_v<ComponentTypes> && ...), "ECS unsafe hot query components must be trivially copyable");
    static_assert((std::is_trivially_destructible_v<ComponentTypes> && ...), "ECS unsafe hot query components must be trivially destructible");

    UnsafeHotReadQuery() = default;

    explicit UnsafeHotReadQuery(const Query<ComponentTypes...>& query, QueryExecutionSettings settings = {}) {
        Rebuild(query, settings);
    }

    // Hot-path plan: no executor telemetry during iteration.
    // Rebuild after structural changes before using stored component pointers.
    [[nodiscard]] bool Rebuild(const Query<ComponentTypes...>& query, QueryExecutionSettings settings = {}) {
        settings_ = settings;
        scratch_.Clear();
        if (!query.IsValid()) {
            valid_ = false;
            cachedStructuralVersion_ = 0U;
            return false;
        }

        query.PrepareBatchExecution(settings_, scratch_);
        StoreComponentIds(query.ComponentIds());
        BuildCachedRangePlan(DefaultRangeSize());
        cachedStructuralVersion_ = query.StructuralVersion();
        valid_ = true;
        return true;
    }

    [[nodiscard]] bool RebuildIfChanged(const Query<ComponentTypes...>& query) {
        if (!valid_ || cachedStructuralVersion_ != query.StructuralVersion()) {
            return Rebuild(query, settings_);
        }
        const std::size_t rangeSize = cachedRangeSize_ == 0U ? DefaultRangeSize() : cachedRangeSize_;
        query.PrepareBatchExecution(settings_, scratch_);
        StoreComponentIds(query.ComponentIds());
        BuildCachedRangePlan(rangeSize);
        return true;
    }

    [[nodiscard]] bool IsValid() const noexcept {
        return valid_;
    }

    [[nodiscard]] bool IsStale(const Query<ComponentTypes...>& query) const noexcept {
        return !valid_ || cachedStructuralVersion_ != query.StructuralVersion();
    }

    [[nodiscard]] std::uint64_t CachedStructuralVersion() const noexcept {
        return cachedStructuralVersion_;
    }

    [[nodiscard]] std::size_t ChunkCount() const noexcept {
        return scratch_.records_.size();
    }

    [[nodiscard]] std::size_t EntityCount() const noexcept {
        std::size_t total = 0U;
        for (const QueryTableDispatchRecord& record : scratch_.records_) {
            total += record.entityCount;
        }
        return total;
    }

    [[nodiscard]] QueryExecutionSettings Settings() const noexcept {
        return settings_;
    }

    [[nodiscard]] std::size_t DefaultRangeSize() const noexcept {
        return ResolveAdaptiveDefaultRangeSize();
    }

    [[nodiscard]] std::size_t CachedRangeSize() const noexcept {
        return cachedRangeSize_;
    }

    [[nodiscard]] std::size_t CachedRangeCount() const noexcept {
        return scratch_.workItems_.size();
    }

    template <typename Kernel>
    void ForEachChunk(Kernel&& kernel) const {
        if (!valid_) {
            return;
        }

        std::size_t rangeIndex = 0U;
        for (const QueryTableDispatchRecord& record : scratch_.records_) {
            if (record.entityCount == 0U) {
                continue;
            }
            Chunk chunk{ record.entityIds, record.entityCount, MakeComponentPointers(record, 0U), rangeIndex++, MakeComponentDirtyCounts(record) };
            std::forward<Kernel>(kernel)(chunk);
        }
    }

    template <typename Kernel>
    UnsafeHotRangeDispatchStats ForEachRange(std::size_t maxRangeSize, Kernel&& kernel) const {
        UnsafeHotRangeDispatchStats stats{};
        if (!valid_) {
            return stats;
        }

        const std::size_t resolvedRangeSize = ResolveRangeSize(maxRangeSize);
        stats.requestedRangeSize = maxRangeSize;
        stats.cachedRangeSize = resolvedRangeSize;
        std::size_t rangeIndex = 0U;
        for (const QueryTableDispatchRecord& record : scratch_.records_) {
            stats.chunks += record.entityCount == 0U ? 0U : 1U;
            for (std::size_t offset = 0U; offset < record.entityCount; offset += resolvedRangeSize) {
                const std::size_t count = record.entityCount - offset < resolvedRangeSize ? record.entityCount - offset : resolvedRangeSize;
                Chunk chunk{ record.entityIds + offset, count, MakeComponentPointers(record, offset), rangeIndex++, MakeComponentDirtyCounts(record) };
                std::forward<Kernel>(kernel)(chunk);
                ++stats.ranges;
                stats.entities += count;
                stats.maxRangeSize = count > stats.maxRangeSize ? count : stats.maxRangeSize;
            }
        }
        PopulateReadTraffic(stats);
        return stats;
    }

    template <typename Kernel>
    UnsafeHotRangeDispatchStats ForEachRangeParallel(
        std::size_t maxRangeSize,
        WorkerPool& workerPool,
        std::size_t workerCountLimit,
        Kernel&& kernel) {
        UnsafeHotRangeDispatchStats stats{};
        if (!valid_) {
            return stats;
        }

        const std::size_t resolvedRangeSize = ResolveRangeSize(maxRangeSize);
        if (resolvedRangeSize != cachedRangeSize_) {
            BuildCachedRangePlan(resolvedRangeSize);
        }
        ApplyCachedWorkerLimit(workerCountLimit);
        stats = CachedDispatchStats();
        stats.requestedRangeSize = maxRangeSize;
        stats.workerCountLimit = workerCountLimit;

        auto chunkJob = [this, &kernel](WorkerContext workerContext, const WorkerPoolChunk& workerChunk) {
            const QueryBatchWorkItem& item = scratch_.workItems_[workerChunk.index];
            const QueryTableDispatchRecord& record = scratch_.records_[item.recordIndex];
            Chunk chunk{ record.entityIds + item.offset, item.count, MakeComponentPointers(record, item.offset), workerChunk.index, MakeComponentDirtyCounts(record) };
            kernel(chunk, workerContext);
        };
        workerPool.ParallelForChunks(scratch_.chunks_, chunkJob);
        return stats;
    }

    template <typename Kernel>
    UnsafeHotRangeDispatchStats ForEachKernel(std::size_t maxRangeSize, Kernel&& kernel) const {
        return ForEachRange(maxRangeSize, std::forward<Kernel>(kernel));
    }

    template <typename Kernel>
    UnsafeHotRangeDispatchStats ForEachKernelParallel(
        std::size_t maxRangeSize,
        WorkerPool& workerPool,
        std::size_t workerCountLimit,
        Kernel&& kernel) {
        return ForEachRangeParallel(maxRangeSize, workerPool, workerCountLimit, std::forward<Kernel>(kernel));
    }

    [[nodiscard]] UnsafeHotRangeDispatchStats CachedDispatchStats() const noexcept {
        UnsafeHotRangeDispatchStats stats{
            .ranges = scratch_.workItems_.size(),
            .cachedRangeSize = cachedRangeSize_,
            .workerCountLimit = cachedWorkerCountLimit_ == std::numeric_limits<std::size_t>::max() ? 0U : cachedWorkerCountLimit_,
        };
        for (const QueryTableDispatchRecord& record : scratch_.records_) {
            stats.chunks += record.entityCount == 0U ? 0U : 1U;
        }
        for (const QueryBatchWorkItem& item : scratch_.workItems_) {
            stats.entities += item.count;
            stats.maxRangeSize = item.count > stats.maxRangeSize ? item.count : stats.maxRangeSize;
        }
        PopulateReadTraffic(stats);
        return stats;
    }

    template <std::size_t DirtyComponentIndex, typename Kernel>
    void ForEachDirtyRange(
        const NativeArchetypeStorage& storage,
        std::size_t maxRangeSize,
        std::vector<NativeComponentDirtyRange>& rangesScratch,
        Kernel&& kernel) const {
        static_assert(DirtyComponentIndex < sizeof...(ComponentTypes), "ECS unsafe hot dirty query component index is out of range");
        if (!valid_ || componentIds_[DirtyComponentIndex] == 0U) {
            return;
        }

        const std::size_t resolvedRangeSize = ResolveRangeSize(maxRangeSize);
        std::size_t rangeIndex = 0U;
        for (const QueryTableDispatchRecord& record : scratch_.records_) {
            if (record.entityCount == 0U) {
                continue;
            }
            if (storage.ComponentDirtyCount(record.nativeArchetypeIndex, record.nativeChunkIndex, componentIds_[DirtyComponentIndex]) == 0U) {
                continue;
            }

            rangesScratch.clear();
            static_cast<void>(storage.CollectComponentDirtyRanges(
                record.nativeArchetypeIndex,
                record.nativeChunkIndex,
                componentIds_[DirtyComponentIndex],
                resolvedRangeSize,
                rangesScratch));
            for (const NativeComponentDirtyRange& range : rangesScratch) {
                if (range.count == 0U || range.dirtyCount == 0U) {
                    continue;
                }
                Chunk chunk{
                    record.entityIds + range.begin,
                    range.count,
                    MakeComponentPointers(record, range.begin),
                    rangeIndex++,
                    MakeCurrentComponentDirtyCounts(storage, record),
                };
                std::forward<Kernel>(kernel)(chunk, range.dirtyCount);
            }
        }
    }

    template <std::size_t DirtyComponentIndex, typename Kernel>
    UnsafeHotDirtyRangeDispatchStats ForEachDirtyRangeParallel(
        const NativeArchetypeStorage& storage,
        std::size_t maxRangeSize,
        WorkerPool& workerPool,
        std::size_t workerCountLimit,
        Kernel&& kernel) {
        static_assert(DirtyComponentIndex < sizeof...(ComponentTypes), "ECS unsafe hot dirty query component index is out of range");
        UnsafeHotDirtyRangeDispatchStats stats{};
        if (!valid_ || componentIds_[DirtyComponentIndex] == 0U) {
            return stats;
        }

        BuildDirtyRangePlan<DirtyComponentIndex>(
            storage,
            ResolveRangeSize(maxRangeSize),
            workerCountLimit,
            stats);
        stats.requestedRangeSize = maxRangeSize;
        stats.workerCountLimit = workerCountLimit;
        PopulateReadTraffic(stats);
        if (dirtyWorkItems_.empty()) {
            return stats;
        }

        auto chunkJob = [this, &storage, &kernel](WorkerContext workerContext, const WorkerPoolChunk& workerChunk) {
            const DirtyRangeWorkItem& item = dirtyWorkItems_[workerChunk.index];
            const QueryTableDispatchRecord& record = scratch_.records_[item.recordIndex];
            Chunk chunk{
                record.entityIds + item.offset,
                item.count,
                MakeComponentPointers(record, item.offset),
                workerChunk.index,
                MakeCurrentComponentDirtyCounts(storage, record),
            };
            kernel(chunk, item.dirtyCount, workerContext);
        };
        workerPool.ParallelForChunks(dirtyChunks_, chunkJob);
        return stats;
    }

private:
    static constexpr std::size_t kComponentBytesPerEntity = (sizeof(ComponentTypes) + ...);

    struct DirtyRangeWorkItem {
        std::size_t recordIndex = 0U;
        std::size_t offset = 0U;
        std::size_t count = 0U;
        std::size_t dirtyCount = 0U;
    };

    void BuildCachedRangePlan(std::size_t rangeSize) {
        cachedRangeSize_ = rangeSize == 0U ? kDefaultQueryExecutionGrainSize : rangeSize;
        cachedWorkerCountLimit_ = std::numeric_limits<std::size_t>::max();
        scratch_.workItems_.clear();
        scratch_.chunks_.clear();
        std::size_t workItemCount = 0U;
        for (const QueryTableDispatchRecord& record : scratch_.records_) {
            workItemCount += record.entityCount == 0U ? 0U : ((record.entityCount - 1U) / cachedRangeSize_) + 1U;
        }
        scratch_.workItems_.reserve(workItemCount);
        scratch_.chunks_.reserve(workItemCount);
        for (std::size_t recordIndex = 0U; recordIndex < scratch_.records_.size(); ++recordIndex) {
            const QueryTableDispatchRecord& record = scratch_.records_[recordIndex];
            for (std::size_t offset = 0U; offset < record.entityCount; offset += cachedRangeSize_) {
                const std::size_t count = record.entityCount - offset < cachedRangeSize_ ? record.entityCount - offset : cachedRangeSize_;
                const std::size_t workIndex = scratch_.workItems_.size();
                scratch_.workItems_.push_back(QueryBatchWorkItem{
                    .recordIndex = recordIndex,
                    .offset = offset,
                    .count = count,
                });
                scratch_.chunks_.push_back(WorkerPoolChunk{
                    .index = workIndex,
                    .begin = offset,
                    .count = count,
                    .preferredWorkerIndex = workIndex,
                    .workerCountLimit = 0U,
                });
            }
        }
    }

    template <std::size_t DirtyComponentIndex>
    void BuildDirtyRangePlan(
        const NativeArchetypeStorage& storage,
        std::size_t rangeSize,
        std::size_t workerCountLimit,
        UnsafeHotDirtyRangeDispatchStats& stats) {
        dirtyWorkItems_.clear();
        dirtyChunks_.clear();
        dirtyRangeBuildScratch_.clear();
        const std::size_t resolvedRangeSize = rangeSize == 0U ? kDefaultQueryExecutionGrainSize : rangeSize;

        std::size_t estimatedRangeCount = 0U;
        for (const QueryTableDispatchRecord& record : scratch_.records_) {
            if (record.entityCount == 0U) {
                continue;
            }
            const std::size_t dirtyCount = storage.ComponentDirtyCount(record.nativeArchetypeIndex, record.nativeChunkIndex, componentIds_[DirtyComponentIndex]);
            if (dirtyCount == 0U) {
                continue;
            }
            estimatedRangeCount += ((dirtyCount - 1U) / resolvedRangeSize) + 1U;
        }
        dirtyWorkItems_.reserve(estimatedRangeCount);
        dirtyChunks_.reserve(estimatedRangeCount);

        for (std::size_t recordIndex = 0U; recordIndex < scratch_.records_.size(); ++recordIndex) {
            const QueryTableDispatchRecord& record = scratch_.records_[recordIndex];
            if (record.entityCount == 0U) {
                continue;
            }
            if (storage.ComponentDirtyCount(record.nativeArchetypeIndex, record.nativeChunkIndex, componentIds_[DirtyComponentIndex]) == 0U) {
                continue;
            }

            dirtyRangeBuildScratch_.clear();
            static_cast<void>(storage.CollectComponentDirtyRanges(
                record.nativeArchetypeIndex,
                record.nativeChunkIndex,
                componentIds_[DirtyComponentIndex],
                resolvedRangeSize,
                dirtyRangeBuildScratch_));
            if (!dirtyRangeBuildScratch_.empty()) {
                ++stats.chunks;
            }
            for (const NativeComponentDirtyRange& range : dirtyRangeBuildScratch_) {
                if (range.count == 0U || range.dirtyCount == 0U) {
                    continue;
                }
                const std::size_t workIndex = dirtyWorkItems_.size();
                dirtyWorkItems_.push_back(DirtyRangeWorkItem{
                    .recordIndex = recordIndex,
                    .offset = range.begin,
                    .count = range.count,
                    .dirtyCount = range.dirtyCount,
                });
                dirtyChunks_.push_back(WorkerPoolChunk{
                    .index = workIndex,
                    .begin = range.begin,
                    .count = range.count,
                    .preferredWorkerIndex = workIndex,
                    .workerCountLimit = workerCountLimit,
                });
                ++stats.ranges;
                stats.entities += range.count;
                stats.dirtyRows += range.dirtyCount;
                stats.maxRangeSize = range.count > stats.maxRangeSize ? range.count : stats.maxRangeSize;
            }
        }
    }

    void StoreComponentIds(std::span<const ComponentId> componentIds) noexcept {
        componentIds_ = {};
        const std::size_t count = componentIds.size() < componentIds_.size() ? componentIds.size() : componentIds_.size();
        for (std::size_t index = 0U; index < count; ++index) {
            componentIds_[index] = componentIds[index];
        }
    }

    void ApplyCachedWorkerLimit(std::size_t workerCountLimit) noexcept {
        if (cachedWorkerCountLimit_ == workerCountLimit) {
            return;
        }
        for (WorkerPoolChunk& chunk : scratch_.chunks_) {
            chunk.workerCountLimit = workerCountLimit;
        }
        cachedWorkerCountLimit_ = workerCountLimit;
    }

    [[nodiscard]] std::size_t ResolveRangeSize(std::size_t requestedRangeSize) const noexcept {
        return requestedRangeSize == 0U ? DefaultRangeSize() : requestedRangeSize;
    }

    [[nodiscard]] std::size_t ResolveAdaptiveDefaultRangeSize() const noexcept {
        const std::size_t fallback = settings_.maxBatchSize == 0U ? kDefaultQueryExecutionGrainSize : settings_.maxBatchSize;
        if (!settings_.adaptiveGrain) {
            return fallback;
        }

        QueryExecutionSettings tuned = TuneQueryExecutionSettings(settings_, QueryExecutionTuningInput{
            .workload = InferWorkload(),
            .entityCount = EntityCount(),
            .workerCount = ResolveSettingsWorkerCount(),
            .fallbackGrainSize = fallback,
        });
        return tuned.maxBatchSize == 0U ? fallback : tuned.maxBatchSize;
    }

    [[nodiscard]] static constexpr QueryExecutionWorkloadClass InferWorkload() noexcept {
        if constexpr (kComponentBytesPerEntity <= 32U) {
            return QueryExecutionWorkloadClass::ReadOnlyMemory;
        } else if constexpr (kComponentBytesPerEntity <= 128U) {
            return QueryExecutionWorkloadClass::MediumKernel;
        } else {
            return QueryExecutionWorkloadClass::DenseMatrixWrite;
        }
    }

    [[nodiscard]] std::size_t ResolveSettingsWorkerCount() const noexcept {
        if (settings_.workerPool == nullptr) {
            return 1U;
        }
        const std::size_t poolWorkers = settings_.workerPool->WorkerCount();
        return settings_.workerCountOverride == 0U ? poolWorkers : std::min(poolWorkers, settings_.workerCountOverride);
    }

    static void PopulateReadTraffic(UnsafeHotRangeDispatchStats& stats) noexcept {
        stats.bytesRead = stats.entities * kComponentBytesPerEntity;
        stats.bytesWritten = 0U;
        stats.bytesTouched = stats.bytesRead;
        stats.bytesPerEntity = stats.entities == 0U ? 0.0 : static_cast<double>(stats.bytesTouched) / static_cast<double>(stats.entities);
    }

    static void PopulateReadTraffic(UnsafeHotDirtyRangeDispatchStats& stats) noexcept {
        stats.bytesRead = stats.entities * kComponentBytesPerEntity;
        stats.bytesWritten = 0U;
        stats.bytesTouched = stats.bytesRead;
        stats.bytesPerEntity = stats.entities == 0U ? 0.0 : static_cast<double>(stats.bytesTouched) / static_cast<double>(stats.entities);
    }

    [[nodiscard]] static typename Chunk::ComponentPointers MakeComponentPointers(
        const QueryTableDispatchRecord& record,
        std::size_t offset) noexcept {
        return ComponentPointersForRecord(record, offset, std::index_sequence_for<ComponentTypes...>{});
    }

    [[nodiscard]] static typename Chunk::ComponentDirtyCounts MakeComponentDirtyCounts(const QueryTableDispatchRecord& record) noexcept {
        return ComponentDirtyCountsForRecord(record, std::index_sequence_for<ComponentTypes...>{});
    }

    [[nodiscard]] typename Chunk::ComponentDirtyCounts MakeCurrentComponentDirtyCounts(
        const NativeArchetypeStorage& storage,
        const QueryTableDispatchRecord& record) const {
        return CurrentComponentDirtyCountsForRecord(storage, record, std::index_sequence_for<ComponentTypes...>{});
    }

    template <std::size_t... Indices>
    [[nodiscard]] static typename Chunk::ComponentPointers ComponentPointersForRecord(
        const QueryTableDispatchRecord& record,
        std::size_t offset,
        std::index_sequence<Indices...>) noexcept {
        return typename Chunk::ComponentPointers{
            (static_cast<const void*>(static_cast<const std::uint8_t*>(record.fieldComponents[Indices]) + offset * sizeof(std::tuple_element_t<Indices, typename Chunk::ComponentTuple>)))...,
        };
    }

    template <std::size_t... Indices>
    [[nodiscard]] static typename Chunk::ComponentDirtyCounts ComponentDirtyCountsForRecord(
        const QueryTableDispatchRecord& record,
        std::index_sequence<Indices...>) noexcept {
        return typename Chunk::ComponentDirtyCounts{ record.componentDirtyCounts[Indices]... };
    }

    template <std::size_t... Indices>
    [[nodiscard]] typename Chunk::ComponentDirtyCounts CurrentComponentDirtyCountsForRecord(
        const NativeArchetypeStorage& storage,
        const QueryTableDispatchRecord& record,
        std::index_sequence<Indices...>) const {
        return typename Chunk::ComponentDirtyCounts{
            storage.ComponentDirtyCount(record.nativeArchetypeIndex, record.nativeChunkIndex, componentIds_[Indices])...,
        };
    }

    QueryExecutionSettings settings_{};
    QueryBatchExecutionScratch scratch_;
    ComponentIdArray componentIds_{};
    std::vector<DirtyRangeWorkItem> dirtyWorkItems_;
    std::vector<WorkerPoolChunk> dirtyChunks_;
    std::vector<NativeComponentDirtyRange> dirtyRangeBuildScratch_;
    std::size_t cachedRangeSize_ = 0U;
    std::size_t cachedWorkerCountLimit_ = std::numeric_limits<std::size_t>::max();
    std::uint64_t cachedStructuralVersion_ = 0U;
    bool valid_ = false;
};

template <typename... ComponentTypes>
class UnsafeHotQuery {
public:
    using MutableChunk = UnsafeHotMutableChunk<ComponentTypes...>;
    using ComponentIdArray = std::array<ComponentId, sizeof...(ComponentTypes)>;

    static_assert(sizeof...(ComponentTypes) > 0, "ECS unsafe hot query must have at least one component");
    static_assert(sizeof...(ComponentTypes) <= kQueryExecutionScratchMaxTerms, "ECS unsafe hot query exceeds the max query term count");
    static_assert((std::is_trivially_copyable_v<ComponentTypes> && ...), "ECS unsafe hot query components must be trivially copyable");
    static_assert((std::is_trivially_destructible_v<ComponentTypes> && ...), "ECS unsafe hot query components must be trivially destructible");

    UnsafeHotQuery() = default;

    explicit UnsafeHotQuery(const Query<ComponentTypes...>& query, QueryExecutionSettings settings = {}) {
        Rebuild(query, settings);
    }

    // Hot-path plan: no borrow validation or executor telemetry during iteration.
    // Rebuild after structural changes before using stored component pointers.
    [[nodiscard]] bool Rebuild(const Query<ComponentTypes...>& query, QueryExecutionSettings settings = {}) {
        settings_ = settings;
        scratch_.Clear();
        if (!query.IsValid()) {
            valid_ = false;
            cachedStructuralVersion_ = 0U;
            return false;
        }

        query.PrepareMutableBatchExecution(settings_, scratch_);
        StoreComponentIds(query.ComponentIds());
        BuildCachedRangePlan(DefaultRangeSize());
        cachedStructuralVersion_ = query.StructuralVersion();
        valid_ = true;
        return true;
    }

    [[nodiscard]] bool RebuildIfChanged(const Query<ComponentTypes...>& query) {
        if (!valid_ || cachedStructuralVersion_ != query.StructuralVersion()) {
            return Rebuild(query, settings_);
        }
        const std::size_t rangeSize = cachedRangeSize_ == 0U ? DefaultRangeSize() : cachedRangeSize_;
        query.PrepareMutableBatchExecution(settings_, scratch_);
        StoreComponentIds(query.ComponentIds());
        BuildCachedRangePlan(rangeSize);
        return true;
    }

    [[nodiscard]] bool IsValid() const noexcept {
        return valid_;
    }

    [[nodiscard]] bool IsStale(const Query<ComponentTypes...>& query) const noexcept {
        return !valid_ || cachedStructuralVersion_ != query.StructuralVersion();
    }

    [[nodiscard]] std::uint64_t CachedStructuralVersion() const noexcept {
        return cachedStructuralVersion_;
    }

    [[nodiscard]] std::size_t ChunkCount() const noexcept {
        return scratch_.mutableRecords_.size();
    }

    [[nodiscard]] std::size_t EntityCount() const noexcept {
        std::size_t total = 0U;
        for (const MutableQueryTableDispatchRecord& record : scratch_.mutableRecords_) {
            total += record.entityCount;
        }
        return total;
    }

    [[nodiscard]] QueryExecutionSettings Settings() const noexcept {
        return settings_;
    }

    [[nodiscard]] std::size_t DefaultRangeSize() const noexcept {
        return ResolveAdaptiveDefaultRangeSize();
    }

    [[nodiscard]] std::size_t CachedRangeSize() const noexcept {
        return cachedRangeSize_;
    }

    [[nodiscard]] std::size_t CachedRangeCount() const noexcept {
        return scratch_.workItems_.size();
    }

    template <typename Kernel>
    void ForEachMutableChunk(Kernel&& kernel) const {
        if (!valid_) {
            return;
        }

        std::size_t rangeIndex = 0U;
        for (const MutableQueryTableDispatchRecord& record : scratch_.mutableRecords_) {
            if (record.entityCount == 0U) {
                continue;
            }
            MutableChunk chunk{ record.entityIds, record.entityCount, MakeComponentPointers(record, 0U), rangeIndex++, MakeComponentDirtyCounts(record) };
            std::forward<Kernel>(kernel)(chunk);
        }
    }

    template <typename Kernel>
    UnsafeHotRangeDispatchStats ForEachMutableRange(std::size_t maxRangeSize, Kernel&& kernel) const {
        UnsafeHotRangeDispatchStats stats{};
        if (!valid_) {
            return stats;
        }

        const std::size_t resolvedRangeSize = ResolveRangeSize(maxRangeSize);
        stats.requestedRangeSize = maxRangeSize;
        stats.cachedRangeSize = resolvedRangeSize;
        std::size_t rangeIndex = 0U;
        for (const MutableQueryTableDispatchRecord& record : scratch_.mutableRecords_) {
            stats.chunks += record.entityCount == 0U ? 0U : 1U;
            for (std::size_t offset = 0U; offset < record.entityCount; offset += resolvedRangeSize) {
                const std::size_t count = record.entityCount - offset < resolvedRangeSize ? record.entityCount - offset : resolvedRangeSize;
                MutableChunk chunk{ record.entityIds + offset, count, MakeComponentPointers(record, offset), rangeIndex++, MakeComponentDirtyCounts(record) };
                std::forward<Kernel>(kernel)(chunk);
                ++stats.ranges;
                stats.entities += count;
                stats.maxRangeSize = count > stats.maxRangeSize ? count : stats.maxRangeSize;
            }
        }
        PopulateMutableTraffic(stats);
        return stats;
    }

    template <typename Kernel>
    UnsafeHotRangeDispatchStats ForEachMutableRangeParallel(
        std::size_t maxRangeSize,
        WorkerPool& workerPool,
        std::size_t workerCountLimit,
        Kernel&& kernel) {
        UnsafeHotRangeDispatchStats stats{};
        if (!valid_) {
            return stats;
        }

        const std::size_t resolvedRangeSize = ResolveRangeSize(maxRangeSize);
        if (resolvedRangeSize != cachedRangeSize_) {
            BuildCachedRangePlan(resolvedRangeSize);
        }
        ApplyCachedWorkerLimit(workerCountLimit);
        stats = CachedDispatchStats();
        stats.requestedRangeSize = maxRangeSize;
        stats.workerCountLimit = workerCountLimit;

        auto chunkJob = [this, &kernel](WorkerContext workerContext, const WorkerPoolChunk& workerChunk) {
            const QueryBatchWorkItem& item = scratch_.workItems_[workerChunk.index];
            const MutableQueryTableDispatchRecord& record = scratch_.mutableRecords_[item.recordIndex];
            MutableChunk chunk{ record.entityIds + item.offset, item.count, MakeComponentPointers(record, item.offset), workerChunk.index, MakeComponentDirtyCounts(record) };
            kernel(chunk, workerContext);
        };
        workerPool.ParallelForChunks(scratch_.chunks_, chunkJob);
        return stats;
    }

    template <typename Kernel>
    UnsafeHotRangeDispatchStats ForEachMutableKernel(std::size_t maxRangeSize, Kernel&& kernel) const {
        return ForEachMutableRange(maxRangeSize, std::forward<Kernel>(kernel));
    }

    template <typename Kernel>
    UnsafeHotRangeDispatchStats ForEachMutableKernelParallel(
        std::size_t maxRangeSize,
        WorkerPool& workerPool,
        std::size_t workerCountLimit,
        Kernel&& kernel) {
        return ForEachMutableRangeParallel(maxRangeSize, workerPool, workerCountLimit, std::forward<Kernel>(kernel));
    }

    [[nodiscard]] UnsafeHotRangeDispatchStats CachedDispatchStats() const noexcept {
        UnsafeHotRangeDispatchStats stats{
            .ranges = scratch_.workItems_.size(),
            .cachedRangeSize = cachedRangeSize_,
            .workerCountLimit = cachedWorkerCountLimit_ == std::numeric_limits<std::size_t>::max() ? 0U : cachedWorkerCountLimit_,
        };
        for (const MutableQueryTableDispatchRecord& record : scratch_.mutableRecords_) {
            stats.chunks += record.entityCount == 0U ? 0U : 1U;
        }
        for (const QueryBatchWorkItem& item : scratch_.workItems_) {
            stats.entities += item.count;
            stats.maxRangeSize = item.count > stats.maxRangeSize ? item.count : stats.maxRangeSize;
        }
        PopulateMutableTraffic(stats);
        return stats;
    }

    template <std::size_t DirtyComponentIndex, typename Kernel>
    UnsafeHotDirtyRangeDispatchStats ForEachDirtyMutableRange(
        NativeArchetypeStorage& storage,
        std::size_t maxRangeSize,
        std::vector<NativeComponentDirtyRange>& rangesScratch,
        bool clearDirtyAfterVisit,
        Kernel&& kernel) const {
        static_assert(DirtyComponentIndex < sizeof...(ComponentTypes), "ECS unsafe hot dirty query component index is out of range");
        UnsafeHotDirtyRangeDispatchStats stats{};
        if (!valid_ || componentIds_[DirtyComponentIndex] == 0U) {
            return stats;
        }

        const std::size_t resolvedRangeSize = ResolveRangeSize(maxRangeSize);
        stats.requestedRangeSize = maxRangeSize;
        std::size_t rangeIndex = 0U;
        for (const MutableQueryTableDispatchRecord& record : scratch_.mutableRecords_) {
            if (record.entityCount == 0U) {
                continue;
            }
            if (storage.ComponentDirtyCount(record.nativeArchetypeIndex, record.nativeChunkIndex, componentIds_[DirtyComponentIndex]) == 0U) {
                continue;
            }

            rangesScratch.clear();
            static_cast<void>(storage.CollectComponentDirtyRanges(
                record.nativeArchetypeIndex,
                record.nativeChunkIndex,
                componentIds_[DirtyComponentIndex],
                resolvedRangeSize,
                rangesScratch));
            if (!rangesScratch.empty()) {
                ++stats.chunks;
            }
            for (const NativeComponentDirtyRange& range : rangesScratch) {
                if (range.count == 0U || range.dirtyCount == 0U) {
                    continue;
                }
                MutableChunk chunk{
                    record.entityIds + range.begin,
                    range.count,
                    MakeComponentPointers(record, range.begin),
                    rangeIndex++,
                    MakeCurrentComponentDirtyCounts(storage, record),
                };
                std::forward<Kernel>(kernel)(chunk, range.dirtyCount);
                ++stats.ranges;
                stats.entities += range.count;
                stats.dirtyRows += range.dirtyCount;
                stats.maxRangeSize = range.count > stats.maxRangeSize ? range.count : stats.maxRangeSize;
                if (clearDirtyAfterVisit) {
                    storage.ClearComponentDirtyRows(
                        record.nativeArchetypeIndex,
                        record.nativeChunkIndex,
                        componentIds_[DirtyComponentIndex],
                        range.begin,
                        range.count);
                }
            }
        }
        PopulateMutableTraffic(stats);
        return stats;
    }

    template <std::size_t DirtyComponentIndex, typename Kernel>
    UnsafeHotDirtyRangeDispatchStats ForEachDirtyMutableRangeParallel(
        NativeArchetypeStorage& storage,
        std::size_t maxRangeSize,
        WorkerPool& workerPool,
        std::size_t workerCountLimit,
        bool clearDirtyAfterVisit,
        Kernel&& kernel) {
        static_assert(DirtyComponentIndex < sizeof...(ComponentTypes), "ECS unsafe hot dirty query component index is out of range");
        UnsafeHotDirtyRangeDispatchStats stats{};
        if (!valid_ || componentIds_[DirtyComponentIndex] == 0U) {
            return stats;
        }

        BuildDirtyMutableRangePlan<DirtyComponentIndex>(
            storage,
            ResolveRangeSize(maxRangeSize),
            workerCountLimit,
            stats);
        stats.requestedRangeSize = maxRangeSize;
        stats.workerCountLimit = workerCountLimit;
        PopulateMutableTraffic(stats);
        if (dirtyWorkItems_.empty()) {
            return stats;
        }

        auto chunkJob = [this, &storage, &kernel](WorkerContext workerContext, const WorkerPoolChunk& workerChunk) {
            const DirtyRangeWorkItem& item = dirtyWorkItems_[workerChunk.index];
            const MutableQueryTableDispatchRecord& record = scratch_.mutableRecords_[item.recordIndex];
            MutableChunk chunk{
                record.entityIds + item.offset,
                item.count,
                MakeComponentPointers(record, item.offset),
                workerChunk.index,
                MakeCurrentComponentDirtyCounts(storage, record),
            };
            kernel(chunk, item.dirtyCount, workerContext);
        };
        workerPool.ParallelForChunks(dirtyChunks_, chunkJob);

        if (clearDirtyAfterVisit) {
            for (const DirtyRangeWorkItem& item : dirtyWorkItems_) {
                const MutableQueryTableDispatchRecord& record = scratch_.mutableRecords_[item.recordIndex];
                storage.ClearComponentDirtyRows(
                    record.nativeArchetypeIndex,
                    record.nativeChunkIndex,
                    componentIds_[DirtyComponentIndex],
                    item.offset,
                    item.count);
            }
        }
        return stats;
    }

    void MarkCachedRangesDirty(NativeArchetypeStorage& storage) const {
        if (!valid_) {
            return;
        }
        const std::span<const ComponentId> componentIds{ componentIds_.data(), componentIds_.size() };
        if (scratch_.workItems_.empty()) {
            for (const MutableQueryTableDispatchRecord& record : scratch_.mutableRecords_) {
                storage.MarkArchetypeChunkComponentsModified(
                    record.nativeArchetypeIndex,
                    record.nativeChunkIndex,
                    0U,
                    record.entityCount,
                    componentIds);
            }
            return;
        }
        for (const QueryBatchWorkItem& item : scratch_.workItems_) {
            const MutableQueryTableDispatchRecord& record = scratch_.mutableRecords_[item.recordIndex];
            storage.MarkArchetypeChunkComponentsModified(
                record.nativeArchetypeIndex,
                record.nativeChunkIndex,
                item.offset,
                item.count,
                componentIds);
        }
    }

private:
    static constexpr std::size_t kComponentBytesPerEntity = (sizeof(ComponentTypes) + ...);

    struct DirtyRangeWorkItem {
        std::size_t recordIndex = 0U;
        std::size_t offset = 0U;
        std::size_t count = 0U;
        std::size_t dirtyCount = 0U;
    };

    void BuildCachedRangePlan(std::size_t rangeSize) {
        cachedRangeSize_ = rangeSize == 0U ? kDefaultQueryExecutionGrainSize : rangeSize;
        cachedWorkerCountLimit_ = std::numeric_limits<std::size_t>::max();
        scratch_.workItems_.clear();
        scratch_.chunks_.clear();
        std::size_t workItemCount = 0U;
        for (const MutableQueryTableDispatchRecord& record : scratch_.mutableRecords_) {
            workItemCount += record.entityCount == 0U ? 0U : ((record.entityCount - 1U) / cachedRangeSize_) + 1U;
        }
        scratch_.workItems_.reserve(workItemCount);
        scratch_.chunks_.reserve(workItemCount);
        for (std::size_t recordIndex = 0U; recordIndex < scratch_.mutableRecords_.size(); ++recordIndex) {
            const MutableQueryTableDispatchRecord& record = scratch_.mutableRecords_[recordIndex];
            for (std::size_t offset = 0U; offset < record.entityCount; offset += cachedRangeSize_) {
                const std::size_t count = record.entityCount - offset < cachedRangeSize_ ? record.entityCount - offset : cachedRangeSize_;
                const std::size_t workIndex = scratch_.workItems_.size();
                scratch_.workItems_.push_back(QueryBatchWorkItem{
                    .recordIndex = recordIndex,
                    .offset = offset,
                    .count = count,
                });
                scratch_.chunks_.push_back(WorkerPoolChunk{
                    .index = workIndex,
                    .begin = offset,
                    .count = count,
                    .preferredWorkerIndex = workIndex,
                    .workerCountLimit = 0U,
                });
            }
        }
    }

    template <std::size_t DirtyComponentIndex>
    void BuildDirtyMutableRangePlan(
        const NativeArchetypeStorage& storage,
        std::size_t rangeSize,
        std::size_t workerCountLimit,
        UnsafeHotDirtyRangeDispatchStats& stats) {
        dirtyWorkItems_.clear();
        dirtyChunks_.clear();
        dirtyRangeBuildScratch_.clear();
        const std::size_t resolvedRangeSize = rangeSize == 0U ? kDefaultQueryExecutionGrainSize : rangeSize;

        std::size_t estimatedRangeCount = 0U;
        for (const MutableQueryTableDispatchRecord& record : scratch_.mutableRecords_) {
            if (record.entityCount == 0U) {
                continue;
            }
            const std::size_t dirtyCount = storage.ComponentDirtyCount(record.nativeArchetypeIndex, record.nativeChunkIndex, componentIds_[DirtyComponentIndex]);
            if (dirtyCount == 0U) {
                continue;
            }
            estimatedRangeCount += ((dirtyCount - 1U) / resolvedRangeSize) + 1U;
        }
        dirtyWorkItems_.reserve(estimatedRangeCount);
        dirtyChunks_.reserve(estimatedRangeCount);

        for (std::size_t recordIndex = 0U; recordIndex < scratch_.mutableRecords_.size(); ++recordIndex) {
            const MutableQueryTableDispatchRecord& record = scratch_.mutableRecords_[recordIndex];
            if (record.entityCount == 0U) {
                continue;
            }
            if (storage.ComponentDirtyCount(record.nativeArchetypeIndex, record.nativeChunkIndex, componentIds_[DirtyComponentIndex]) == 0U) {
                continue;
            }

            dirtyRangeBuildScratch_.clear();
            static_cast<void>(storage.CollectComponentDirtyRanges(
                record.nativeArchetypeIndex,
                record.nativeChunkIndex,
                componentIds_[DirtyComponentIndex],
                resolvedRangeSize,
                dirtyRangeBuildScratch_));
            if (!dirtyRangeBuildScratch_.empty()) {
                ++stats.chunks;
            }
            for (const NativeComponentDirtyRange& range : dirtyRangeBuildScratch_) {
                if (range.count == 0U || range.dirtyCount == 0U) {
                    continue;
                }
                const std::size_t workIndex = dirtyWorkItems_.size();
                dirtyWorkItems_.push_back(DirtyRangeWorkItem{
                    .recordIndex = recordIndex,
                    .offset = range.begin,
                    .count = range.count,
                    .dirtyCount = range.dirtyCount,
                });
                dirtyChunks_.push_back(WorkerPoolChunk{
                    .index = workIndex,
                    .begin = range.begin,
                    .count = range.count,
                    .preferredWorkerIndex = workIndex,
                    .workerCountLimit = workerCountLimit,
                });
                ++stats.ranges;
                stats.entities += range.count;
                stats.dirtyRows += range.dirtyCount;
                stats.maxRangeSize = range.count > stats.maxRangeSize ? range.count : stats.maxRangeSize;
            }
        }
    }

    void StoreComponentIds(std::span<const ComponentId> componentIds) noexcept {
        componentIds_ = {};
        const std::size_t count = componentIds.size() < componentIds_.size() ? componentIds.size() : componentIds_.size();
        for (std::size_t index = 0U; index < count; ++index) {
            componentIds_[index] = componentIds[index];
        }
    }

    void ApplyCachedWorkerLimit(std::size_t workerCountLimit) noexcept {
        if (cachedWorkerCountLimit_ == workerCountLimit) {
            return;
        }
        for (WorkerPoolChunk& chunk : scratch_.chunks_) {
            chunk.workerCountLimit = workerCountLimit;
        }
        cachedWorkerCountLimit_ = workerCountLimit;
    }

    [[nodiscard]] std::size_t ResolveRangeSize(std::size_t requestedRangeSize) const noexcept {
        return requestedRangeSize == 0U ? DefaultRangeSize() : requestedRangeSize;
    }

    [[nodiscard]] std::size_t ResolveAdaptiveDefaultRangeSize() const noexcept {
        const std::size_t fallback = settings_.maxBatchSize == 0U ? kDefaultQueryExecutionGrainSize : settings_.maxBatchSize;
        if (!settings_.adaptiveGrain) {
            return fallback;
        }

        QueryExecutionSettings tuned = TuneQueryExecutionSettings(settings_, QueryExecutionTuningInput{
            .workload = InferWorkload(),
            .entityCount = EntityCount(),
            .workerCount = ResolveSettingsWorkerCount(),
            .fallbackGrainSize = fallback,
        });
        return tuned.maxBatchSize == 0U ? fallback : tuned.maxBatchSize;
    }

    [[nodiscard]] static constexpr QueryExecutionWorkloadClass InferWorkload() noexcept {
        constexpr std::size_t mutableBytesPerEntity = kComponentBytesPerEntity * 2U;
        if constexpr (mutableBytesPerEntity <= 64U) {
            return QueryExecutionWorkloadClass::LinearWrite;
        } else if constexpr (mutableBytesPerEntity <= 160U) {
            return QueryExecutionWorkloadClass::MediumKernel;
        } else if constexpr (mutableBytesPerEntity <= 384U) {
            return QueryExecutionWorkloadClass::DenseMatrixWrite;
        } else {
            return QueryExecutionWorkloadClass::HeavyTransform;
        }
    }

    [[nodiscard]] std::size_t ResolveSettingsWorkerCount() const noexcept {
        if (settings_.workerPool == nullptr) {
            return 1U;
        }
        const std::size_t poolWorkers = settings_.workerPool->WorkerCount();
        return settings_.workerCountOverride == 0U ? poolWorkers : std::min(poolWorkers, settings_.workerCountOverride);
    }

    static void PopulateMutableTraffic(UnsafeHotRangeDispatchStats& stats) noexcept {
        stats.bytesRead = stats.entities * kComponentBytesPerEntity;
        stats.bytesWritten = stats.entities * kComponentBytesPerEntity;
        stats.bytesTouched = stats.bytesRead + stats.bytesWritten;
        stats.bytesPerEntity = stats.entities == 0U ? 0.0 : static_cast<double>(stats.bytesTouched) / static_cast<double>(stats.entities);
    }

    static void PopulateMutableTraffic(UnsafeHotDirtyRangeDispatchStats& stats) noexcept {
        stats.bytesRead = stats.entities * kComponentBytesPerEntity;
        stats.bytesWritten = stats.entities * kComponentBytesPerEntity;
        stats.bytesTouched = stats.bytesRead + stats.bytesWritten;
        stats.bytesPerEntity = stats.entities == 0U ? 0.0 : static_cast<double>(stats.bytesTouched) / static_cast<double>(stats.entities);
    }

    [[nodiscard]] static typename MutableChunk::ComponentPointers MakeComponentPointers(
        const MutableQueryTableDispatchRecord& record,
        std::size_t offset) noexcept {
        return ComponentPointersForRecord(record, offset, std::index_sequence_for<ComponentTypes...>{});
    }

    [[nodiscard]] static typename MutableChunk::ComponentDirtyCounts MakeComponentDirtyCounts(const MutableQueryTableDispatchRecord& record) noexcept {
        return ComponentDirtyCountsForRecord(record, std::index_sequence_for<ComponentTypes...>{});
    }

    [[nodiscard]] typename MutableChunk::ComponentDirtyCounts MakeCurrentComponentDirtyCounts(
        const NativeArchetypeStorage& storage,
        const MutableQueryTableDispatchRecord& record) const {
        return CurrentComponentDirtyCountsForRecord(storage, record, std::index_sequence_for<ComponentTypes...>{});
    }

    template <std::size_t... Indices>
    [[nodiscard]] static typename MutableChunk::ComponentPointers ComponentPointersForRecord(
        const MutableQueryTableDispatchRecord& record,
        std::size_t offset,
        std::index_sequence<Indices...>) noexcept {
        return typename MutableChunk::ComponentPointers{
            (static_cast<void*>(static_cast<std::uint8_t*>(record.fieldComponents[Indices]) + offset * sizeof(std::tuple_element_t<Indices, typename MutableChunk::ComponentTuple>)))...,
        };
    }

    template <std::size_t... Indices>
    [[nodiscard]] static typename MutableChunk::ComponentDirtyCounts ComponentDirtyCountsForRecord(
        const MutableQueryTableDispatchRecord& record,
        std::index_sequence<Indices...>) noexcept {
        return typename MutableChunk::ComponentDirtyCounts{ record.componentDirtyCounts[Indices]... };
    }

    template <std::size_t... Indices>
    [[nodiscard]] typename MutableChunk::ComponentDirtyCounts CurrentComponentDirtyCountsForRecord(
        const NativeArchetypeStorage& storage,
        const MutableQueryTableDispatchRecord& record,
        std::index_sequence<Indices...>) const {
        return typename MutableChunk::ComponentDirtyCounts{
            storage.ComponentDirtyCount(record.nativeArchetypeIndex, record.nativeChunkIndex, componentIds_[Indices])...,
        };
    }

    QueryExecutionSettings settings_{};
    QueryBatchExecutionScratch scratch_;
    ComponentIdArray componentIds_{};
    std::vector<DirtyRangeWorkItem> dirtyWorkItems_;
    std::vector<WorkerPoolChunk> dirtyChunks_;
    std::vector<NativeComponentDirtyRange> dirtyRangeBuildScratch_;
    std::size_t cachedRangeSize_ = 0U;
    std::size_t cachedWorkerCountLimit_ = std::numeric_limits<std::size_t>::max();
    std::uint64_t cachedStructuralVersion_ = 0U;
    bool valid_ = false;
};

} // namespace kb::ecs
