#include "ecs/QueryState.hpp"

#include "ecs/query/QueryPlan.hpp"
#include "ecs/query/QueryTableBatchDispatcher.hpp"
#include "ecs/query/QueryLimits.hpp"
#include "engine/ecs/MutableComponentBorrowLocks.hpp"
#include "engine/ecs/NativeArchetypeStorage.hpp"
#include "engine/ecs/StructuralChangeValidator.hpp"
#include "engine/ecs/WorkerPool.hpp"
#include "engine/ecs/WorldTelemetry.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <utility>

namespace kb::ecs {
namespace {

thread_local QueryWorkerContext tCurrentQueryWorkerContext{};

class ScopedQueryWorkerContext {
public:
    explicit ScopedQueryWorkerContext(WorkerContext workerContext) noexcept
        : previous_(tCurrentQueryWorkerContext) {
        tCurrentQueryWorkerContext = QueryWorkerContext{
            .workerIndex = workerContext.workerIndex,
            .workerCount = workerContext.workerCount,
            .active = true,
        };
    }

    ~ScopedQueryWorkerContext() {
        tCurrentQueryWorkerContext = previous_;
    }

    ScopedQueryWorkerContext(const ScopedQueryWorkerContext&) = delete;
    ScopedQueryWorkerContext& operator=(const ScopedQueryWorkerContext&) = delete;

private:
    QueryWorkerContext previous_{};
};

[[nodiscard]] std::size_t ResolveBatchSize(QueryExecutionSettings settings, std::size_t defaultExecutionGrainSize) noexcept {
    const std::size_t resolved = settings.maxBatchSize == 0 ? defaultExecutionGrainSize : settings.maxBatchSize;
    return resolved == 0 ? kDefaultQueryExecutionGrainSize : resolved;
}

[[nodiscard]] bool IsDeterministicExecution(QueryExecutionSettings settings) noexcept {
    return settings.iterationOrder == QueryIterationOrder::Deterministic
        || settings.policy == QueryExecutionPolicy::Deterministic
        || settings.reductionMode == QueryReductionMode::Deterministic;
}

[[nodiscard]] bool CanExecuteInParallel(QueryExecutionSettings settings) noexcept {
    return settings.workerPool != nullptr
        && !IsDeterministicExecution(settings)
        && settings.policy != QueryExecutionPolicy::SingleThread;
}

[[nodiscard]] bool ShouldSplitParallelRanges(QueryExecutionSettings settings) noexcept {
    return settings.policy == QueryExecutionPolicy::ParallelRanges || settings.policy == QueryExecutionPolicy::SIMDPreferred;
}

[[nodiscard]] std::size_t ResolveQueryWorkerCount(QueryExecutionSettings settings) noexcept {
    if (settings.workerPool == nullptr) {
        return 1U;
    }
    const std::size_t poolWorkerCount = settings.workerPool->WorkerCount();
    return settings.workerCountOverride == 0U
        ? poolWorkerCount
        : std::min(poolWorkerCount, settings.workerCountOverride);
}

[[nodiscard]] bool ShouldRecordQueryTelemetry(const WorldTelemetryCounters* counters, QueryExecutionSettings settings) noexcept {
    return counters != nullptr && settings.telemetryEnabled;
}

[[nodiscard]] std::chrono::steady_clock::time_point BeginQueryTelemetryTiming(
    const WorldTelemetryCounters* counters,
    QueryExecutionSettings settings) noexcept {
    return ShouldRecordQueryTelemetry(counters, settings) ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
}

[[nodiscard]] std::uint64_t EndQueryTelemetryTiming(
    const WorldTelemetryCounters* counters,
    QueryExecutionSettings settings,
    std::chrono::steady_clock::time_point startedAt) noexcept {
    if (!ShouldRecordQueryTelemetry(counters, settings)) {
        return 0;
    }

    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - startedAt).count());
}

void RecordQueryExecutionTelemetry(
    WorldTelemetryCounters* counters,
    QueryExecutionSettings settings,
    std::size_t entityCount,
    std::uint64_t bytesTouched,
    std::uint64_t elapsedNanoseconds,
    std::size_t batchCount,
    std::size_t parallelWorkItems) noexcept {
    if (!ShouldRecordQueryTelemetry(counters, settings)) {
        return;
    }

    ++counters->queryExecutions;
    counters->queryBatches += batchCount;
    counters->queryEntitiesVisited += entityCount;
    counters->queryBytesTouched += bytesTouched;
    counters->queryElapsedNanoseconds += elapsedNanoseconds;
    if (settings.workerPool == nullptr || parallelWorkItems == 0U) {
        return;
    }

    const std::size_t workerCount = ResolveQueryWorkerCount(settings);
    if (workerCount == 0U) {
        return;
    }
    ++counters->queryParallelExecutions;
    counters->queryWorkerSlots += workerCount;
    counters->queryWorkerActiveSlots += std::min(workerCount, parallelWorkItems);
}

[[nodiscard]] std::size_t QueryBytesPerEntity(std::span<const std::size_t> componentSizes) noexcept {
    std::size_t bytesPerEntity = 0;
    for (std::size_t componentSize : componentSizes) {
        bytesPerEntity += componentSize;
    }
    return bytesPerEntity;
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

#if !defined(NDEBUG)
struct MutableBorrowDispatchContext {
    MutableComponentBorrowLocks* locks = nullptr;
    std::span<const ComponentId> componentIds;
    std::span<const std::size_t> componentSizes;
    QueryRawMutableBatchVisitor visitor = nullptr;
    void* context = nullptr;
};

void DispatchMutableBorrowedBatch(
    const Entity::IdType* entityIds,
    std::size_t count,
    void* const* components,
    void* context) {
    auto* borrowContext = static_cast<MutableBorrowDispatchContext*>(context);
    if (borrowContext == nullptr || borrowContext->visitor == nullptr) {
        return;
    }

    if (borrowContext->locks == nullptr) {
        borrowContext->visitor(entityIds, count, components, borrowContext->context);
        return;
    }

    std::array<MutableComponentBorrowRange, kMaxQueryTerms> ranges{};
    for (std::size_t field = 0; field < borrowContext->componentIds.size(); ++field) {
        ranges[field] = MutableComponentBorrowRange{
            .componentId = borrowContext->componentIds[field],
            .data = components[field],
            .bytes = borrowContext->componentSizes[field] * count,
        };
    }

    MutableComponentBorrowLocks::Guard guard = borrowContext->locks->Acquire(
        std::span<const MutableComponentBorrowRange>{ ranges.data(), borrowContext->componentIds.size() });
    borrowContext->visitor(entityIds, count, components, borrowContext->context);
}
#endif

} // namespace

QueryWorkerContext CurrentQueryWorkerContext() noexcept {
    return tCurrentQueryWorkerContext;
}

QueryState::QueryState(
    NativeArchetypeStorage* nativeStorage,
    std::shared_ptr<QueryPlan> plan,
    std::size_t defaultExecutionGrainSize,
    MutableComponentBorrowLocks* mutableBorrowLocks,
    StructuralChangeValidator* structuralChangeValidator,
    WorldTelemetryCounters* telemetryCounters)
    : nativeStorage_(nativeStorage)
    , plan_(std::move(plan))
    , mutableBorrowLocks_(mutableBorrowLocks)
    , structuralChangeValidator_(structuralChangeValidator)
    , telemetryCounters_(telemetryCounters)
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

void QueryState::PrepareMutableBatchExecution(QueryExecutionSettings settings, QueryBatchExecutionScratch& scratch) const {
    if (!IsValid()) {
        scratch.Clear();
        return;
    }

    static_cast<void>(settings);
    nativeStorage_->CollectMutableQueryRecords(
        plan_->ComponentIds(),
        plan_->RequiredComponentIds(),
        plan_->ExcludedComponentIds(),
        scratch.mutableRecords_);
    scratch.records_.clear();
    scratch.workItems_.clear();
    scratch.chunks_.clear();
}

void QueryState::ForEach(QueryRawVisitor visitor, void* context) const {
    if (!IsValid() || visitor == nullptr) {
        return;
    }

    [[maybe_unused]] StructuralChangeValidator::Guard iterationGuard =
        structuralChangeValidator_ != nullptr ? structuralChangeValidator_->EnterIteration() : StructuralChangeValidator::Guard{};

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

    [[maybe_unused]] StructuralChangeValidator::Guard iterationGuard =
        structuralChangeValidator_ != nullptr ? structuralChangeValidator_->EnterIteration() : StructuralChangeValidator::Guard{};

    nativeStorage_->CollectQueryRecords(
        plan_->ComponentIds(),
        plan_->RequiredComponentIds(),
        plan_->ExcludedComponentIds(),
        scratch.records_);

    if (IsDeterministicExecution(settings)) {
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
    const bool splitParallelRanges = ShouldSplitParallelRanges(settings);
    const std::size_t telemetryBytesPerEntity = QueryBytesPerEntity(plan_->ComponentSizes());
    const auto telemetryStartedAt = BeginQueryTelemetryTiming(telemetryCounters_, settings);
    if (CanExecuteInParallel(settings)) {
        const std::size_t queryWorkerCount = ResolveQueryWorkerCount(settings);
        scratch.workItems_.clear();
        scratch.chunks_.clear();
        std::size_t telemetryEntityCount = 0;
        for (std::size_t recordIndex = 0; recordIndex < scratch.records_.size(); ++recordIndex) {
            const QueryTableDispatchRecord& record = scratch.records_[recordIndex];
            if (plan_->HasChangeFilters() && !RecordChanged(record)) {
                CommitRecordVersions(record);
                continue;
            }
            const std::size_t parallelStep = splitParallelRanges ? maxBatchSize : record.entityCount;
            for (std::size_t offset = 0; offset < record.entityCount; offset += parallelStep) {
                const std::size_t count = std::min(parallelStep, record.entityCount - offset);
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
                    .preferredWorkerIndex = workIndex,
                    .workerCountLimit = queryWorkerCount,
                });
                telemetryEntityCount += count;
            }
            CommitRecordVersions(record);
        }
        auto chunkJob = [&records = scratch.records_, componentSizes = plan_->ComponentSizes(), &workItems = scratch.workItems_, visitor, context, prefetchDistance = settings.prefetchDistance](WorkerContext workerContext, const WorkerPoolChunk& chunk) {
            const ScopedQueryWorkerContext scopedWorker{ workerContext };
            const QueryBatchWorkItem& item = workItems[chunk.index];
            DispatchReadOnlyRecordBatch(records[item.recordIndex], componentSizes, item.offset, item.count, prefetchDistance, visitor, context);
        };
        settings.workerPool->ParallelForChunks(scratch.chunks_, chunkJob);
        RecordQueryExecutionTelemetry(
            telemetryCounters_,
            settings,
            telemetryEntityCount,
            static_cast<std::uint64_t>(telemetryEntityCount) * telemetryBytesPerEntity,
            EndQueryTelemetryTiming(telemetryCounters_, settings, telemetryStartedAt),
            scratch.workItems_.size(),
            scratch.workItems_.size());
        return;
    }

    std::size_t telemetryEntityCount = 0;
    std::size_t telemetryBatchCount = 0;
    for (const QueryTableDispatchRecord& record : scratch.records_) {
        if (plan_->HasChangeFilters() && !RecordChanged(record)) {
            CommitRecordVersions(record);
            continue;
        }
        telemetryEntityCount += record.entityCount;
        telemetryBatchCount += (record.entityCount + maxBatchSize - 1U) / maxBatchSize;
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
    RecordQueryExecutionTelemetry(
        telemetryCounters_,
        settings,
        telemetryEntityCount,
        static_cast<std::uint64_t>(telemetryEntityCount) * telemetryBytesPerEntity,
        EndQueryTelemetryTiming(telemetryCounters_, settings, telemetryStartedAt),
        telemetryBatchCount,
        0);
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

    [[maybe_unused]] StructuralChangeValidator::Guard iterationGuard =
        structuralChangeValidator_ != nullptr ? structuralChangeValidator_->EnterIteration() : StructuralChangeValidator::Guard{};

    nativeStorage_->CollectMutableQueryRecords(
        plan_->ComponentIds(),
        plan_->RequiredComponentIds(),
        plan_->ExcludedComponentIds(),
        scratch.mutableRecords_);

    if (IsDeterministicExecution(settings)) {
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
    const bool splitParallelRanges = ShouldSplitParallelRanges(settings);
    const std::size_t telemetryBytesPerEntity = QueryBytesPerEntity(plan_->ComponentSizes());
    const auto telemetryStartedAt = BeginQueryTelemetryTiming(telemetryCounters_, settings);
#if !defined(NDEBUG)
    MutableBorrowDispatchContext borrowContext{
        .locks = mutableBorrowLocks_,
        .componentIds = plan_->ComponentIds(),
        .componentSizes = plan_->ComponentSizes(),
        .visitor = visitor,
        .context = context,
    };
    QueryRawMutableBatchVisitor dispatchVisitor = mutableBorrowLocks_ == nullptr ? visitor : &DispatchMutableBorrowedBatch;
    void* dispatchContext = mutableBorrowLocks_ == nullptr ? context : &borrowContext;
#else
    QueryRawMutableBatchVisitor dispatchVisitor = visitor;
    void* dispatchContext = context;
#endif
    if (CanExecuteInParallel(settings)) {
        const std::size_t queryWorkerCount = ResolveQueryWorkerCount(settings);
        scratch.workItems_.clear();
        scratch.chunks_.clear();
        std::size_t telemetryEntityCount = 0;
        for (std::size_t recordIndex = 0; recordIndex < scratch.mutableRecords_.size(); ++recordIndex) {
            const MutableQueryTableDispatchRecord& record = scratch.mutableRecords_[recordIndex];
            if (plan_->HasChangeFilters() && !RecordChanged(record)) {
                CommitRecordVersions(record);
                continue;
            }
            const std::size_t parallelStep = splitParallelRanges ? maxBatchSize : record.entityCount;
            for (std::size_t offset = 0; offset < record.entityCount; offset += parallelStep) {
                const std::size_t count = std::min(parallelStep, record.entityCount - offset);
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
                    .preferredWorkerIndex = workIndex,
                    .workerCountLimit = queryWorkerCount,
                });
                telemetryEntityCount += count;
            }
        }
        auto chunkJob = [&records = scratch.mutableRecords_, componentSizes = plan_->ComponentSizes(), &workItems = scratch.workItems_, dispatchVisitor, dispatchContext, prefetchDistance = settings.prefetchDistance](WorkerContext workerContext, const WorkerPoolChunk& chunk) {
            const ScopedQueryWorkerContext scopedWorker{ workerContext };
            const QueryBatchWorkItem& item = workItems[chunk.index];
            DispatchMutableRecordBatch(records[item.recordIndex], componentSizes, item.offset, item.count, prefetchDistance, dispatchVisitor, dispatchContext);
        };
        settings.workerPool->ParallelForChunks(scratch.chunks_, chunkJob);

        for (const MutableQueryTableDispatchRecord& record : scratch.mutableRecords_) {
            if (plan_->HasChangeFilters() && !RecordChanged(record)) {
                continue;
            }
            nativeStorage_->MarkArchetypeComponentsModified(record.nativeArchetypeIndex, plan_->ComponentIds());
            CommitRecordVersions(record);
        }
        RecordQueryExecutionTelemetry(
            telemetryCounters_,
            settings,
            telemetryEntityCount,
            static_cast<std::uint64_t>(telemetryEntityCount) * telemetryBytesPerEntity,
            EndQueryTelemetryTiming(telemetryCounters_, settings, telemetryStartedAt),
            scratch.workItems_.size(),
            scratch.workItems_.size());
        return;
    }

    std::size_t telemetryEntityCount = 0;
    std::size_t telemetryBatchCount = 0;
    for (const MutableQueryTableDispatchRecord& record : scratch.mutableRecords_) {
        if (plan_->HasChangeFilters() && !RecordChanged(record)) {
            CommitRecordVersions(record);
            continue;
        }
        telemetryEntityCount += record.entityCount;
        telemetryBatchCount += (record.entityCount + maxBatchSize - 1U) / maxBatchSize;
        QueryTableBatchDispatcher::DispatchMutable(
            record.entityIds,
            record.entityCount,
            plan_->ComponentSizes(),
            record.fieldComponents,
            maxBatchSize,
            settings.prefetchDistance,
            dispatchVisitor,
            dispatchContext);
        nativeStorage_->MarkArchetypeComponentsModified(record.nativeArchetypeIndex, plan_->ComponentIds());
        CommitRecordVersions(record);
    }
    RecordQueryExecutionTelemetry(
        telemetryCounters_,
        settings,
        telemetryEntityCount,
        static_cast<std::uint64_t>(telemetryEntityCount) * telemetryBytesPerEntity,
        EndQueryTelemetryTiming(telemetryCounters_, settings, telemetryStartedAt),
        telemetryBatchCount,
        0);
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

void PrepareQueryStateMutableBatchExecution(const QueryState* state, QueryExecutionSettings settings, QueryBatchExecutionScratch& scratch) {
    if (state != nullptr) {
        state->PrepareMutableBatchExecution(settings, scratch);
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
