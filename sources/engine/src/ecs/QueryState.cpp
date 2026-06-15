#include "ecs/QueryState.hpp"

#include "ecs/query/QueryPlan.hpp"
#include "ecs/query/QueryTableBatchDispatcher.hpp"
#include "engine/ecs/NativeArchetypeStorage.hpp"
#include "engine/ecs/WorkerPool.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace kb::ecs {
namespace {

[[nodiscard]] std::size_t ResolveBatchSize(QueryExecutionSettings settings, std::size_t defaultExecutionGrainSize) noexcept {
    const std::size_t resolved = settings.maxBatchSize == 0 ? defaultExecutionGrainSize : settings.maxBatchSize;
    return resolved == 0 ? kDefaultQueryExecutionGrainSize : resolved;
}

void DispatchReadOnlyRecordBatch(
    const QueryTableDispatchRecord& record,
    std::span<const std::size_t> componentSizes,
    std::size_t offset,
    std::size_t count,
    std::size_t prefetchDistance,
    QueryRawBatchVisitor visitor,
    void* context) {
    QueryComponentPointerBlock batchComponents{};
    for (std::size_t field = 0; field < componentSizes.size(); ++field) {
        const auto* bytes = static_cast<const std::uint8_t*>(record.fieldComponents[field]);
        batchComponents[field] = bytes + offset * componentSizes[field];
    }

    QueryTableBatchDispatcher::Dispatch(
        record.entityIds + offset,
        count,
        componentSizes,
        batchComponents,
        count,
        prefetchDistance,
        visitor,
        context);
}

void DispatchMutableRecordBatch(
    const MutableQueryTableDispatchRecord& record,
    std::span<const std::size_t> componentSizes,
    std::size_t offset,
    std::size_t count,
    std::size_t prefetchDistance,
    QueryRawMutableBatchVisitor visitor,
    void* context) {
    MutableQueryComponentPointerBlock batchComponents{};
    for (std::size_t field = 0; field < componentSizes.size(); ++field) {
        auto* bytes = static_cast<std::uint8_t*>(record.fieldComponents[field]);
        batchComponents[field] = bytes + offset * componentSizes[field];
    }

    QueryTableBatchDispatcher::DispatchMutable(
        record.entityIds + offset,
        count,
        componentSizes,
        batchComponents,
        count,
        prefetchDistance,
        visitor,
        context);
}

} // namespace

QueryState::QueryState(NativeArchetypeStorage* nativeStorage, std::shared_ptr<QueryPlan> plan, std::size_t defaultExecutionGrainSize)
    : nativeStorage_(nativeStorage)
    , plan_(std::move(plan))
    , defaultExecutionGrainSize_(defaultExecutionGrainSize == 0 ? kDefaultQueryExecutionGrainSize : defaultExecutionGrainSize) {}

bool QueryState::IsValid() const noexcept {
    return nativeStorage_ != nullptr && plan_ && plan_->IsValid();
}

std::span<const ComponentId> QueryState::ComponentIds() const noexcept {
    if (!plan_) {
        return {};
    }
    return plan_->ComponentIds();
}

std::span<const std::size_t> QueryState::ComponentSizes() const noexcept {
    if (!plan_) {
        return {};
    }
    return plan_->ComponentSizes();
}

void QueryState::PrepareBatchExecution(QueryExecutionSettings settings, QueryBatchExecutionScratch& scratch) const {
    if (!IsValid()) {
        scratch.Clear();
        return;
    }

    static_cast<void>(settings);
    nativeStorage_->CollectQueryRecords(
        plan_->ComponentIds(),
        plan_->RequiredComponentIds(),
        plan_->ExcludedComponentIds(),
        scratch.records_);
    scratch.mutableRecords_.clear();
    scratch.workItems_.clear();
    scratch.chunks_.clear();
}

void QueryState::ForEach(QueryRawVisitor visitor, void* context) const {
    if (!IsValid() || visitor == nullptr) {
        return;
    }

    QueryBatchExecutionScratch scratch;
    nativeStorage_->CollectQueryRecords(
        plan_->ComponentIds(),
        plan_->RequiredComponentIds(),
        plan_->ExcludedComponentIds(),
        scratch.records_);

    QueryComponentPointerBlock rowComponents{};
    for (const QueryTableDispatchRecord& record : scratch.records_) {
        if (plan_->HasChangeFilters() && !RecordChanged(record)) {
            continue;
        }
        for (std::size_t row = 0; row < record.entityCount; ++row) {
            for (std::size_t field = 0; field < plan_->ComponentSizes().size(); ++field) {
                const auto* bytes = static_cast<const std::uint8_t*>(record.fieldComponents[field]);
                rowComponents[field] = bytes + row * plan_->ComponentSizes()[field];
            }
            visitor(Entity{ record.entityIds[row] }, rowComponents.data(), context);
        }
        CommitRecordVersions(record);
    }
}

void QueryState::ForEachBatch(QueryExecutionSettings settings, QueryRawBatchVisitor visitor, void* context) const {
    if (!IsValid() || visitor == nullptr) {
        return;
    }

    QueryBatchExecutionScratch scratch;
    ForEachBatch(settings, visitor, context, scratch);
}

void QueryState::ForEachBatch(QueryExecutionSettings settings, QueryRawBatchVisitor visitor, void* context, QueryBatchExecutionScratch& scratch) const {
    if (!IsValid() || visitor == nullptr) {
        return;
    }

    nativeStorage_->CollectQueryRecords(
        plan_->ComponentIds(),
        plan_->RequiredComponentIds(),
        plan_->ExcludedComponentIds(),
        scratch.records_);

    if (settings.iterationOrder == QueryIterationOrder::Deterministic) {
        std::sort(scratch.records_.begin(), scratch.records_.end(), [](const QueryTableDispatchRecord& left, const QueryTableDispatchRecord& right) {
            if (left.nativeArchetypeIndex != right.nativeArchetypeIndex) {
                return left.nativeArchetypeIndex < right.nativeArchetypeIndex;
            }
            if (left.firstEntityId != right.firstEntityId) {
                return left.firstEntityId < right.firstEntityId;
            }
            return left.sequence < right.sequence;
        });
    }

    const std::size_t maxBatchSize = ResolveBatchSize(settings, defaultExecutionGrainSize_);
    if (settings.workerPool != nullptr && settings.iterationOrder != QueryIterationOrder::Deterministic) {
        scratch.workItems_.clear();
        scratch.chunks_.clear();
        for (std::size_t recordIndex = 0; recordIndex < scratch.records_.size(); ++recordIndex) {
            const QueryTableDispatchRecord& record = scratch.records_[recordIndex];
            if (plan_->HasChangeFilters() && !RecordChanged(record)) {
                CommitRecordVersions(record);
                continue;
            }
            for (std::size_t offset = 0; offset < record.entityCount; offset += maxBatchSize) {
                const std::size_t count = std::min(maxBatchSize, record.entityCount - offset);
                const std::size_t workIndex = scratch.workItems_.size();
                scratch.workItems_.push_back(QueryBatchWorkItem{
                    .recordIndex = recordIndex,
                    .offset = offset,
                    .count = count,
                });
                scratch.chunks_.push_back(WorkerPoolChunk{
                    .index = workIndex,
                    .begin = offset,
                    .count = count,
                    .preferredWorkerIndex = recordIndex,
                });
            }
            CommitRecordVersions(record);
        }

        auto chunkJob = [&records = scratch.records_, componentSizes = plan_->ComponentSizes(), &workItems = scratch.workItems_, visitor, context, prefetchDistance = settings.prefetchDistance](WorkerContext, const WorkerPoolChunk& chunk) {
            const QueryBatchWorkItem& item = workItems[chunk.index];
            DispatchReadOnlyRecordBatch(records[item.recordIndex], componentSizes, item.offset, item.count, prefetchDistance, visitor, context);
        };
        settings.workerPool->ParallelForChunks(scratch.chunks_, chunkJob);
        return;
    }

    for (const QueryTableDispatchRecord& record : scratch.records_) {
        if (plan_->HasChangeFilters() && !RecordChanged(record)) {
            CommitRecordVersions(record);
            continue;
        }
        QueryTableBatchDispatcher::Dispatch(
            record.entityIds,
            record.entityCount,
            plan_->ComponentSizes(),
            record.fieldComponents,
            maxBatchSize,
            settings.prefetchDistance,
            visitor,
            context);
        CommitRecordVersions(record);
    }
}

void QueryState::ForEachMutableBatch(QueryExecutionSettings settings, QueryRawMutableBatchVisitor visitor, void* context) const {
    if (!IsValid() || visitor == nullptr) {
        return;
    }

    QueryBatchExecutionScratch scratch;
    ForEachMutableBatch(settings, visitor, context, scratch);
}

void QueryState::ForEachMutableBatch(QueryExecutionSettings settings, QueryRawMutableBatchVisitor visitor, void* context, QueryBatchExecutionScratch& scratch) const {
    if (!IsValid() || visitor == nullptr) {
        return;
    }

    nativeStorage_->CollectMutableQueryRecords(
        plan_->ComponentIds(),
        plan_->RequiredComponentIds(),
        plan_->ExcludedComponentIds(),
        scratch.mutableRecords_);

    if (settings.iterationOrder == QueryIterationOrder::Deterministic) {
        std::sort(scratch.mutableRecords_.begin(), scratch.mutableRecords_.end(), [](const MutableQueryTableDispatchRecord& left, const MutableQueryTableDispatchRecord& right) {
            if (left.nativeArchetypeIndex != right.nativeArchetypeIndex) {
                return left.nativeArchetypeIndex < right.nativeArchetypeIndex;
            }
            if (left.firstEntityId != right.firstEntityId) {
                return left.firstEntityId < right.firstEntityId;
            }
            return left.sequence < right.sequence;
        });
    }

    const std::size_t maxBatchSize = ResolveBatchSize(settings, defaultExecutionGrainSize_);
    for (const MutableQueryTableDispatchRecord& record : scratch.mutableRecords_) {
        if (plan_->HasChangeFilters() && !RecordChanged(record)) {
            CommitRecordVersions(record);
            continue;
        }
        QueryTableBatchDispatcher::DispatchMutable(
            record.entityIds,
            record.entityCount,
            plan_->ComponentSizes(),
            record.fieldComponents,
            maxBatchSize,
            settings.prefetchDistance,
            visitor,
            context);
        nativeStorage_->MarkArchetypeComponentsModified(record.nativeArchetypeIndex, plan_->ComponentIds());
        CommitRecordVersions(record);
    }
}

bool QueryState::RecordChanged(const QueryTableDispatchRecord& record) const {
    if (!plan_->HasChangeFilters()) {
        return true;
    }
    for (ComponentId componentId : plan_->ChangedComponentIds()) {
        const std::uint64_t version = nativeStorage_->ArchetypeComponentVersion(record.nativeArchetypeIndex, componentId);
        const ChangeVersionKey key{ .archetypeIndex = record.nativeArchetypeIndex, .componentId = componentId };
        const auto observed = observedVersions_.find(key);
        if (observed == observedVersions_.end() || observed->second != version) {
            return true;
        }
    }
    return false;
}

bool QueryState::RecordChanged(const MutableQueryTableDispatchRecord& record) const {
    if (!plan_->HasChangeFilters()) {
        return true;
    }
    for (ComponentId componentId : plan_->ChangedComponentIds()) {
        const std::uint64_t version = nativeStorage_->ArchetypeComponentVersion(record.nativeArchetypeIndex, componentId);
        const ChangeVersionKey key{ .archetypeIndex = record.nativeArchetypeIndex, .componentId = componentId };
        const auto observed = observedVersions_.find(key);
        if (observed == observedVersions_.end() || observed->second != version) {
            return true;
        }
    }
    return false;
}

void QueryState::CommitRecordVersions(const QueryTableDispatchRecord& record) const {
    for (ComponentId componentId : plan_->ChangedComponentIds()) {
        observedVersions_[ChangeVersionKey{ .archetypeIndex = record.nativeArchetypeIndex, .componentId = componentId }] =
            nativeStorage_->ArchetypeComponentVersion(record.nativeArchetypeIndex, componentId);
    }
}

void QueryState::CommitRecordVersions(const MutableQueryTableDispatchRecord& record) const {
    for (ComponentId componentId : plan_->ChangedComponentIds()) {
        observedVersions_[ChangeVersionKey{ .archetypeIndex = record.nativeArchetypeIndex, .componentId = componentId }] =
            nativeStorage_->ArchetypeComponentVersion(record.nativeArchetypeIndex, componentId);
    }
}

void DestroyQueryState(QueryState* state) noexcept {
    delete state;
}

bool IsQueryStateValid(const QueryState* state) noexcept {
    return state != nullptr && state->IsValid();
}

void ForEachQueryState(const QueryState* state, QueryRawVisitor visitor, void* context) {
    if (state != nullptr) {
        state->ForEach(visitor, context);
    }
}

void PrepareQueryStateBatchExecution(const QueryState* state, QueryExecutionSettings settings, QueryBatchExecutionScratch& scratch) {
    if (state != nullptr) {
        state->PrepareBatchExecution(settings, scratch);
    }
}

void ForEachQueryStateBatch(const QueryState* state, QueryExecutionSettings settings, QueryRawBatchVisitor visitor, void* context) {
    if (state != nullptr) {
        state->ForEachBatch(settings, visitor, context);
    }
}

void ForEachQueryStateBatch(const QueryState* state, QueryExecutionSettings settings, QueryRawBatchVisitor visitor, void* context, QueryBatchExecutionScratch& scratch) {
    if (state != nullptr) {
        state->ForEachBatch(settings, visitor, context, scratch);
    }
}

void ForEachQueryStateMutableBatch(const QueryState* state, QueryExecutionSettings settings, QueryRawMutableBatchVisitor visitor, void* context) {
    if (state != nullptr) {
        state->ForEachMutableBatch(settings, visitor, context);
    }
}

void ForEachQueryStateMutableBatch(const QueryState* state, QueryExecutionSettings settings, QueryRawMutableBatchVisitor visitor, void* context, QueryBatchExecutionScratch& scratch) {
    if (state != nullptr) {
        state->ForEachMutableBatch(settings, visitor, context, scratch);
    }
}

} // namespace kb::ecs
