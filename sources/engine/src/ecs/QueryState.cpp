#include "ecs/QueryState.hpp"

#include "ecs/query/QueryPlan.hpp"
#include "ecs/query/QueryTableBatchDispatcher.hpp"
#include "ecs/query/QueryLimits.hpp"
#include "engine/ecs/MutableComponentBorrowLocks.hpp"
#include "engine/ecs/NativeArchetypeStorage.hpp"
#include "engine/ecs/QueryExecutionTuning.hpp"
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
thread_local QueryBatchExecutionScratch tBatchExecutionScratch{};
thread_local bool tBatchExecutionScratchInUse = false;

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

class ScopedThreadLocalBatchScratch {
public:
    explicit ScopedThreadLocalBatchScratch(bool& inUse) noexcept
        : inUse_(inUse) {
        inUse_ = true;
    }

    ~ScopedThreadLocalBatchScratch() {
        inUse_ = false;
    }

    ScopedThreadLocalBatchScratch(const ScopedThreadLocalBatchScratch&) = delete;
    ScopedThreadLocalBatchScratch& operator=(const ScopedThreadLocalBatchScratch&) = delete;

private:
    bool& inUse_;
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

[[nodiscard]] QueryExecutionSettings ResolveQuerySettings(
    QueryExecutionSettings settings,
    std::size_t defaultPrefetchDistance) noexcept {
    if (settings.prefetchDistance == 0U) {
        settings.prefetchDistance = defaultPrefetchDistance;
    }
    return settings;
}

[[nodiscard]] bool CanExecuteInParallel(QueryExecutionSettings settings) noexcept {
    return settings.workerPool != nullptr
        && !IsDeterministicExecution(settings)
        && QueryExecutionPolicyUsesParallelism(settings.policy);
}

[[nodiscard]] bool ShouldSplitParallelRanges(QueryExecutionSettings settings) noexcept {
    // Range-based splitting for every parallel policy except the chunk policy,
    // which dispatches whole chunks.
    return QueryExecutionPolicyUsesParallelism(settings.policy)
        && settings.policy != QueryExecutionPolicy::ParallelChunks;
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
    std::uint64_t kernelElapsedNanoseconds,
    std::size_t effectiveBatchSize,
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
    counters->queryKernelElapsedNanoseconds += kernelElapsedNanoseconds;
    counters->queryAdaptiveExecutions += settings.adaptiveGrain ? 1U : 0U;
    counters->queryEffectiveBatchSizeTotal += effectiveBatchSize;
    counters->queryMaxEffectiveBatchSize = std::max<std::uint64_t>(
        counters->queryMaxEffectiveBatchSize,
        static_cast<std::uint64_t>(effectiveBatchSize));
    counters->queryPrefetchDistanceTotal += settings.prefetchDistance;
    switch (settings.policy) {
    case QueryExecutionPolicy::SingleThread:
    case QueryExecutionPolicy::SingleThreadSIMD:
        ++counters->querySingleThreadExecutions;
        break;
    case QueryExecutionPolicy::ParallelChunks:
        ++counters->queryParallelChunkExecutions;
        break;
    case QueryExecutionPolicy::ParallelRanges:
    case QueryExecutionPolicy::StreamingLargeWorld:
        ++counters->queryParallelRangeExecutions;
        break;
    case QueryExecutionPolicy::SIMDPreferred:
    case QueryExecutionPolicy::ParallelSIMD:
        ++counters->querySimdPreferredExecutions;
        break;
    case QueryExecutionPolicy::Deterministic:
        ++counters->queryDeterministicExecutions;
        break;
    }
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

void RecordQueryPrepareTelemetry(
    WorldTelemetryCounters* counters,
    QueryExecutionSettings settings,
    std::size_t recordCount,
    std::size_t archetypeCount,
    std::uint64_t elapsedNanoseconds) noexcept {
    if (!ShouldRecordQueryTelemetry(counters, settings)) {
        return;
    }

    ++counters->queryPrepareCalls;
    counters->queryPrepareRecords += recordCount;
    counters->queryMatchedChunks += recordCount;
    counters->queryMatchedArchetypes += archetypeCount;
    counters->queryPrepareElapsedNanoseconds += elapsedNanoseconds;
}

template <typename Record>
[[nodiscard]] std::size_t CountUniqueNativeArchetypes(std::span<const Record> records) noexcept {
    std::size_t archetypeCount = 0;
    for (std::size_t index = 0; index < records.size(); ++index) {
        bool seen = false;
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (records[previous].nativeArchetypeIndex == records[index].nativeArchetypeIndex) {
                seen = true;
                break;
            }
        }
        archetypeCount += seen ? 0U : 1U;
    }
    return archetypeCount;
}

[[nodiscard]] std::size_t QueryBytesPerEntity(std::span<const std::size_t> componentSizes) noexcept {
    std::size_t bytesPerEntity = 0;
    for (std::size_t componentSize : componentSizes) {
        bytesPerEntity += componentSize;
    }
    return bytesPerEntity;
}

[[nodiscard]] std::size_t MutableQueryBytesPerEntity(std::span<const std::size_t> componentSizes) noexcept {
    return QueryBytesPerEntity(componentSizes) * 2U;
}

template <typename Record>
[[nodiscard]] std::size_t CountRecordEntities(std::span<const Record> records) noexcept {
    std::size_t entityCount = 0;
    for (const Record& record : records) {
        entityCount += record.entityCount;
    }
    return entityCount;
}

template <typename Record>
[[nodiscard]] std::size_t EstimateQueryWorkItems(
    std::span<const Record> records,
    std::size_t maxBatchSize,
    bool splitParallelRanges) noexcept {
    std::size_t workItems = 0;
    for (const Record& record : records) {
        if (record.entityCount == 0U) {
            continue;
        }
        const std::size_t step = splitParallelRanges ? maxBatchSize : record.entityCount;
        workItems += ((record.entityCount - 1U) / step) + 1U;
    }
    return workItems;
}

[[nodiscard]] QueryExecutionWorkloadClass InferReadOnlyWorkload(std::span<const std::size_t> componentSizes) noexcept {
    const std::size_t bytesPerEntity = QueryBytesPerEntity(componentSizes);
    if (bytesPerEntity <= 32U) {
        return QueryExecutionWorkloadClass::ReadOnlyMemory;
    }
    if (bytesPerEntity <= 128U) {
        return QueryExecutionWorkloadClass::MediumKernel;
    }
    return QueryExecutionWorkloadClass::DenseMatrixWrite;
}

[[nodiscard]] QueryExecutionWorkloadClass InferMutableWorkload(std::span<const std::size_t> componentSizes) noexcept {
    const std::size_t bytesPerEntity = MutableQueryBytesPerEntity(componentSizes);
    if (bytesPerEntity <= 64U) {
        return QueryExecutionWorkloadClass::LinearWrite;
    }
    if (bytesPerEntity <= 160U) {
        return QueryExecutionWorkloadClass::MediumKernel;
    }
    if (bytesPerEntity <= 384U) {
        return QueryExecutionWorkloadClass::DenseMatrixWrite;
    }
    return QueryExecutionWorkloadClass::HeavyTransform;
}

template <typename Record>
[[nodiscard]] QueryExecutionSettings ResolveAdaptiveQuerySettings(
    QueryExecutionSettings settings,
    std::span<const Record> records,
    QueryExecutionWorkloadClass workload,
    std::size_t defaultExecutionGrainSize) noexcept {
    if (!settings.adaptiveGrain) {
        return settings;
    }

    return TuneQueryExecutionSettings(settings, QueryExecutionTuningInput{
        .workload = workload,
        .entityCount = CountRecordEntities(records),
        .workerCount = ResolveQueryWorkerCount(settings),
        .fallbackGrainSize = defaultExecutionGrainSize,
    });
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
    std::size_t defaultPrefetchDistance,
    MutableComponentBorrowLocks* mutableBorrowLocks,
    StructuralChangeValidator* structuralChangeValidator,
    WorldTelemetryCounters* telemetryCounters)
    : nativeStorage_(nativeStorage)
    , plan_(std::move(plan))
    , mutableBorrowLocks_(mutableBorrowLocks)
    , structuralChangeValidator_(structuralChangeValidator)
    , telemetryCounters_(telemetryCounters)
    , defaultExecutionGrainSize_(defaultExecutionGrainSize == 0 ? kDefaultQueryExecutionGrainSize : defaultExecutionGrainSize)
    , defaultPrefetchDistance_(defaultPrefetchDistance) {}

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

std::uint64_t QueryState::StructuralVersion() const noexcept {
    return nativeStorage_ != nullptr ? nativeStorage_->StructuralVersion() : 0;
}

void QueryState::PrepareBatchExecution(QueryExecutionSettings settings, QueryBatchExecutionScratch& scratch) const {
    if (!IsValid()) {
        scratch.Clear();
        return;
    }

    const auto prepareStartedAt = BeginQueryTelemetryTiming(telemetryCounters_, settings);
    PrepareReadRecords(settings, scratch, true);
    scratch.mutableRecords_.clear();
    scratch.workItems_.clear();
    scratch.chunks_.clear();
    RecordQueryPrepareTelemetry(
        telemetryCounters_,
        settings,
        scratch.records_.size(),
        ShouldRecordQueryTelemetry(telemetryCounters_, settings)
            ? CountUniqueNativeArchetypes(std::span<const QueryTableDispatchRecord>{ scratch.records_.data(), scratch.records_.size() })
            : 0U,
        EndQueryTelemetryTiming(telemetryCounters_, settings, prepareStartedAt));
}

void QueryState::PrepareMutableBatchExecution(QueryExecutionSettings settings, QueryBatchExecutionScratch& scratch) const {
    if (!IsValid()) {
        scratch.Clear();
        return;
    }

    const auto prepareStartedAt = BeginQueryTelemetryTiming(telemetryCounters_, settings);
    PrepareMutableRecords(settings, scratch, true);
    scratch.records_.clear();
    scratch.workItems_.clear();
    scratch.chunks_.clear();
    RecordQueryPrepareTelemetry(
        telemetryCounters_,
        settings,
        scratch.mutableRecords_.size(),
        ShouldRecordQueryTelemetry(telemetryCounters_, settings)
            ? CountUniqueNativeArchetypes(std::span<const MutableQueryTableDispatchRecord>{ scratch.mutableRecords_.data(), scratch.mutableRecords_.size() })
            : 0U,
        EndQueryTelemetryTiming(telemetryCounters_, settings, prepareStartedAt));
}

void QueryState::ForEach(QueryRawVisitor visitor, void* context) const {
    if (!IsValid() || visitor == nullptr) {
        return;
    }

    [[maybe_unused]] StructuralChangeValidator::Guard iterationGuard =
        structuralChangeValidator_ != nullptr ? structuralChangeValidator_->EnterIteration() : StructuralChangeValidator::Guard{};

    QueryBatchExecutionScratch scratch;
    PrepareReadRecords(QueryExecutionSettings{}, scratch, false);

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

    if (!tBatchExecutionScratchInUse) {
        ScopedThreadLocalBatchScratch scratchUse{ tBatchExecutionScratchInUse };
        ForEachBatch(settings, visitor, context, tBatchExecutionScratch);
        return;
    }

    QueryBatchExecutionScratch scratch;
    ForEachBatch(settings, visitor, context, scratch);
}

void QueryState::ForEachBatch(QueryExecutionSettings settings, QueryRawBatchVisitor visitor, void* context, QueryBatchExecutionScratch& scratch) const {
    if (!IsValid() || visitor == nullptr) {
        return;
    }
    settings = ResolveQuerySettings(settings, defaultPrefetchDistance_);

    [[maybe_unused]] StructuralChangeValidator::Guard iterationGuard =
        structuralChangeValidator_ != nullptr ? structuralChangeValidator_->EnterIteration() : StructuralChangeValidator::Guard{};

    const auto prepareStartedAt = BeginQueryTelemetryTiming(telemetryCounters_, settings);
    PrepareReadRecords(settings, scratch, false);

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
    RecordQueryPrepareTelemetry(
        telemetryCounters_,
        settings,
        scratch.records_.size(),
        ShouldRecordQueryTelemetry(telemetryCounters_, settings)
            ? CountUniqueNativeArchetypes(std::span<const QueryTableDispatchRecord>{ scratch.records_.data(), scratch.records_.size() })
            : 0U,
        EndQueryTelemetryTiming(telemetryCounters_, settings, prepareStartedAt));

    settings = ResolveAdaptiveQuerySettings(
        settings,
        std::span<const QueryTableDispatchRecord>{ scratch.records_.data(), scratch.records_.size() },
        InferReadOnlyWorkload(plan_->ComponentSizes()),
        defaultExecutionGrainSize_);
    const std::size_t maxBatchSize = ResolveBatchSize(settings, defaultExecutionGrainSize_);
    const bool splitParallelRanges = ShouldSplitParallelRanges(settings);
    const std::size_t telemetryBytesPerEntity = QueryBytesPerEntity(plan_->ComponentSizes());
    const auto telemetryStartedAt = BeginQueryTelemetryTiming(telemetryCounters_, settings);
    if (CanExecuteInParallel(settings)) {
        const std::size_t queryWorkerCount = ResolveQueryWorkerCount(settings);
        scratch.workItems_.clear();
        scratch.chunks_.clear();
        const std::size_t estimatedWorkItems = EstimateQueryWorkItems(
            std::span<const QueryTableDispatchRecord>{ scratch.records_.data(), scratch.records_.size() },
            maxBatchSize,
            splitParallelRanges);
        scratch.workItems_.reserve(estimatedWorkItems);
        scratch.chunks_.reserve(estimatedWorkItems);
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
        const auto kernelStartedAt = BeginQueryTelemetryTiming(telemetryCounters_, settings);
        settings.workerPool->ParallelForChunks(scratch.chunks_, chunkJob);
        const std::uint64_t kernelElapsedNanoseconds = EndQueryTelemetryTiming(telemetryCounters_, settings, kernelStartedAt);
        RecordQueryExecutionTelemetry(
            telemetryCounters_,
            settings,
            telemetryEntityCount,
            static_cast<std::uint64_t>(telemetryEntityCount) * telemetryBytesPerEntity,
            EndQueryTelemetryTiming(telemetryCounters_, settings, telemetryStartedAt),
            kernelElapsedNanoseconds,
            maxBatchSize,
            scratch.workItems_.size(),
            scratch.workItems_.size());
        return;
    }

    std::size_t telemetryEntityCount = 0;
    std::size_t telemetryBatchCount = 0;
    const auto kernelStartedAt = BeginQueryTelemetryTiming(telemetryCounters_, settings);
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
    const std::uint64_t kernelElapsedNanoseconds = EndQueryTelemetryTiming(telemetryCounters_, settings, kernelStartedAt);
    RecordQueryExecutionTelemetry(
        telemetryCounters_,
        settings,
        telemetryEntityCount,
        static_cast<std::uint64_t>(telemetryEntityCount) * telemetryBytesPerEntity,
        EndQueryTelemetryTiming(telemetryCounters_, settings, telemetryStartedAt),
        kernelElapsedNanoseconds,
        maxBatchSize,
        telemetryBatchCount,
        0);
}

void QueryState::ForEachMutableBatch(QueryExecutionSettings settings, QueryRawMutableBatchVisitor visitor, void* context) const {
    if (!IsValid() || visitor == nullptr) {
        return;
    }

    if (!tBatchExecutionScratchInUse) {
        ScopedThreadLocalBatchScratch scratchUse{ tBatchExecutionScratchInUse };
        ForEachMutableBatch(settings, visitor, context, tBatchExecutionScratch);
        return;
    }

    QueryBatchExecutionScratch scratch;
    ForEachMutableBatch(settings, visitor, context, scratch);
}

void QueryState::ForEachMutableBatch(QueryExecutionSettings settings, QueryRawMutableBatchVisitor visitor, void* context, QueryBatchExecutionScratch& scratch) const {
    if (!IsValid() || visitor == nullptr) {
        return;
    }
    settings = ResolveQuerySettings(settings, defaultPrefetchDistance_);

    [[maybe_unused]] StructuralChangeValidator::Guard iterationGuard =
        structuralChangeValidator_ != nullptr ? structuralChangeValidator_->EnterIteration() : StructuralChangeValidator::Guard{};

    const auto prepareStartedAt = BeginQueryTelemetryTiming(telemetryCounters_, settings);
    PrepareMutableRecords(settings, scratch, false);

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
    RecordQueryPrepareTelemetry(
        telemetryCounters_,
        settings,
        scratch.mutableRecords_.size(),
        ShouldRecordQueryTelemetry(telemetryCounters_, settings)
            ? CountUniqueNativeArchetypes(std::span<const MutableQueryTableDispatchRecord>{ scratch.mutableRecords_.data(), scratch.mutableRecords_.size() })
            : 0U,
        EndQueryTelemetryTiming(telemetryCounters_, settings, prepareStartedAt));

    settings = ResolveAdaptiveQuerySettings(
        settings,
        std::span<const MutableQueryTableDispatchRecord>{ scratch.mutableRecords_.data(), scratch.mutableRecords_.size() },
        InferMutableWorkload(plan_->ComponentSizes()),
        defaultExecutionGrainSize_);
    const std::size_t maxBatchSize = ResolveBatchSize(settings, defaultExecutionGrainSize_);
    const bool splitParallelRanges = ShouldSplitParallelRanges(settings);
    const std::size_t telemetryBytesPerEntity = MutableQueryBytesPerEntity(plan_->ComponentSizes());
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
        const std::size_t estimatedWorkItems = EstimateQueryWorkItems(
            std::span<const MutableQueryTableDispatchRecord>{ scratch.mutableRecords_.data(), scratch.mutableRecords_.size() },
            maxBatchSize,
            splitParallelRanges);
        scratch.workItems_.reserve(estimatedWorkItems);
        scratch.chunks_.reserve(estimatedWorkItems);
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
        const auto kernelStartedAt = BeginQueryTelemetryTiming(telemetryCounters_, settings);
        settings.workerPool->ParallelForChunks(scratch.chunks_, chunkJob);
        const std::uint64_t kernelElapsedNanoseconds = EndQueryTelemetryTiming(telemetryCounters_, settings, kernelStartedAt);

        for (const QueryBatchWorkItem& item : scratch.workItems_) {
            const MutableQueryTableDispatchRecord& record = scratch.mutableRecords_[item.recordIndex];
            nativeStorage_->MarkArchetypeChunkComponentsModified(
                record.nativeArchetypeIndex,
                record.nativeChunkIndex,
                item.offset,
                item.count,
                plan_->ComponentIds());
        }
        for (const MutableQueryTableDispatchRecord& record : scratch.mutableRecords_) {
            if (!plan_->HasChangeFilters() || RecordChanged(record)) {
                CommitRecordVersions(record);
            }
        }
        RecordQueryExecutionTelemetry(
            telemetryCounters_,
            settings,
            telemetryEntityCount,
            static_cast<std::uint64_t>(telemetryEntityCount) * telemetryBytesPerEntity,
            EndQueryTelemetryTiming(telemetryCounters_, settings, telemetryStartedAt),
            kernelElapsedNanoseconds,
            maxBatchSize,
            scratch.workItems_.size(),
            scratch.workItems_.size());
        return;
    }

    std::size_t telemetryEntityCount = 0;
    std::size_t telemetryBatchCount = 0;
    const auto kernelStartedAt = BeginQueryTelemetryTiming(telemetryCounters_, settings);
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
    const std::uint64_t kernelElapsedNanoseconds = EndQueryTelemetryTiming(telemetryCounters_, settings, kernelStartedAt);
    RecordQueryExecutionTelemetry(
        telemetryCounters_,
        settings,
        telemetryEntityCount,
        static_cast<std::uint64_t>(telemetryEntityCount) * telemetryBytesPerEntity,
        EndQueryTelemetryTiming(telemetryCounters_, settings, telemetryStartedAt),
        kernelElapsedNanoseconds,
        maxBatchSize,
        telemetryBatchCount,
        0);
}

void QueryState::PrepareReadRecords(QueryExecutionSettings settings, QueryBatchExecutionScratch& scratch, bool refreshMetadata) const {
    if (nativeStorage_ == nullptr || !plan_) {
        scratch.records_.clear();
        return;
    }

    const std::uint64_t structuralVersion = nativeStorage_->StructuralVersion();
    const bool recordCacheTelemetry = ShouldRecordQueryTelemetry(telemetryCounters_, settings);
    if (cachedReadStructuralVersion_ != structuralVersion) {
        if (recordCacheTelemetry) {
            ++telemetryCounters_->queryRecordCacheMisses;
        }
        nativeStorage_->CollectQueryRecords(
            plan_->ComponentIds(),
            plan_->RequiredComponentIds(),
            plan_->ExcludedComponentIds(),
            cachedReadRecords_);
        cachedReadStructuralVersion_ = structuralVersion;
    } else if (recordCacheTelemetry) {
        ++telemetryCounters_->queryRecordCacheHits;
    }

    scratch.records_ = cachedReadRecords_;
    if (refreshMetadata) {
        RefreshRecordMetadata(std::span<QueryTableDispatchRecord>{ scratch.records_.data(), scratch.records_.size() });
    }
}

void QueryState::PrepareMutableRecords(QueryExecutionSettings settings, QueryBatchExecutionScratch& scratch, bool refreshMetadata) const {
    if (nativeStorage_ == nullptr || !plan_) {
        scratch.mutableRecords_.clear();
        return;
    }

    const std::uint64_t structuralVersion = nativeStorage_->StructuralVersion();
    const bool recordCacheTelemetry = ShouldRecordQueryTelemetry(telemetryCounters_, settings);
    if (cachedMutableStructuralVersion_ != structuralVersion) {
        if (recordCacheTelemetry) {
            ++telemetryCounters_->queryRecordCacheMisses;
        }
        nativeStorage_->CollectMutableQueryRecords(
            plan_->ComponentIds(),
            plan_->RequiredComponentIds(),
            plan_->ExcludedComponentIds(),
            cachedMutableRecords_);
        cachedMutableStructuralVersion_ = structuralVersion;
    } else if (recordCacheTelemetry) {
        ++telemetryCounters_->queryRecordCacheHits;
    }

    scratch.mutableRecords_ = cachedMutableRecords_;
    if (refreshMetadata) {
        RefreshRecordMetadata(std::span<MutableQueryTableDispatchRecord>{ scratch.mutableRecords_.data(), scratch.mutableRecords_.size() });
    }
}

void QueryState::RefreshRecordMetadata(std::span<QueryTableDispatchRecord> records) const {
    if (nativeStorage_ == nullptr || !plan_) {
        return;
    }

    const std::span<const ComponentId> componentIds = plan_->ComponentIds();
    for (QueryTableDispatchRecord& record : records) {
        for (std::size_t field = 0; field < componentIds.size(); ++field) {
            const ComponentId componentId = componentIds[field];
            record.componentVersions[field] = nativeStorage_->ArchetypeComponentVersion(record.nativeArchetypeIndex, componentId);
            record.componentDirtyCounts[field] =
                nativeStorage_->ComponentDirtyCount(record.nativeArchetypeIndex, record.nativeChunkIndex, componentId);
        }
    }
}

void QueryState::RefreshRecordMetadata(std::span<MutableQueryTableDispatchRecord> records) const {
    if (nativeStorage_ == nullptr || !plan_) {
        return;
    }

    const std::span<const ComponentId> componentIds = plan_->ComponentIds();
    for (MutableQueryTableDispatchRecord& record : records) {
        for (std::size_t field = 0; field < componentIds.size(); ++field) {
            const ComponentId componentId = componentIds[field];
            record.componentVersions[field] = nativeStorage_->ArchetypeComponentVersion(record.nativeArchetypeIndex, componentId);
            record.componentDirtyCounts[field] =
                nativeStorage_->ComponentDirtyCount(record.nativeArchetypeIndex, record.nativeChunkIndex, componentId);
        }
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

std::span<const ComponentId> QueryStateComponentIds(const QueryState* state) noexcept {
    return state != nullptr ? state->ComponentIds() : std::span<const ComponentId>{};
}

std::uint64_t QueryStateStructuralVersion(const QueryState* state) noexcept {
    return state != nullptr ? state->StructuralVersion() : 0;
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
