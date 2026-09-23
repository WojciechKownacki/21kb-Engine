#include "engine/ecs/World.hpp"

#include "engine/ecs/Kernel.hpp"
#include "engine/ecs/MemoryTrafficEstimator.hpp"

#include "ecs/FlecsEntityIds.hpp"

#include <flecs.h>

#include <mutex>

namespace kb::ecs {

bool World::Progress(float deltaSeconds) {
    return world_ != nullptr && ecs_progress(world_, deltaSeconds);
}

void World::RequestQuit() noexcept {
    if (world_ != nullptr) {
        ecs_quit(world_);
    }
}

bool World::ShouldQuit() const noexcept {
    return world_ == nullptr || ecs_should_quit(world_);
}

ecs_world_t* World::NativeHandle() noexcept {
    return world_;
}

const ecs_world_t* World::NativeHandle() const noexcept {
    return world_;
}

bool World::BackendEntityAlive(Entity entity) const noexcept {
    return world_ != nullptr && entity.IsValid() && ecs_is_alive(world_, FlecsEntityId(entity));
}

const NativeArchetypeStorage& World::NativeStorage() const noexcept {
    return *nativeStorage_;
}

NativeEcsStorageStats World::NativeStorageStats() const {
    return nativeStorage_ != nullptr ? nativeStorage_->Stats() : NativeEcsStorageStats{};
}

NativeEcsMaintenanceStats World::MaintainNativeStorage(NativeEcsMaintenanceBudget budget) {
    return nativeStorage_ != nullptr ? nativeStorage_->MaintainChunks(budget) : NativeEcsMaintenanceStats{};
}

std::size_t World::NativeChunkPayloadBytes() const noexcept {
    return nativeStorage_ != nullptr ? nativeStorage_->ChunkPayloadBytes() : 0;
}

QueryExecutionSettings World::DefaultQueryExecutionSettings(WorkerPool* workerPool, QueryExecutionPolicy policy) const noexcept {
    return QueryExecutionSettings{
        .maxBatchSize = config_.executionGrainSize,
        .policy = policy,
        .prefetchDistance = config_.queryPrefetchDistance,
        .workerCountOverride = config_.workerThreadLimit,
        .workerPool = workerPool,
        .adaptiveGrain = config_.adaptiveQueryExecution,
    };
}

WorldTelemetrySnapshot World::TelemetrySnapshot() const {
    const std::lock_guard lock(telemetryState_->mutex);
    const NativeEcsStorageStats storageStats = NativeStorageStats();
    const std::size_t capacity = storageStats.capacity;
    const double occupancyPercent = capacity == 0 ? 0.0 : (static_cast<double>(storageStats.liveEntities) * 100.0) / static_cast<double>(capacity);
    const double chunkCount = static_cast<double>(storageStats.chunks);
    const double sparseChunkPercent =
        chunkCount == 0.0 ? 0.0 : (static_cast<double>(storageStats.sparseChunks) * 100.0) / chunkCount;
    const double tailSparseChunkPercent =
        chunkCount == 0.0 ? 0.0 : (static_cast<double>(storageStats.tailSparseChunks) * 100.0) / chunkCount;
    const double fragmentedChunkPercent =
        chunkCount == 0.0 ? 0.0 : (static_cast<double>(storageStats.fragmentedChunks) * 100.0) / chunkCount;
    const double emptyChunkPercent =
        chunkCount == 0.0 ? 0.0 : (static_cast<double>(storageStats.emptyChunks) * 100.0) / chunkCount;
    const double queryPlanRequests = static_cast<double>(telemetryState_->counters.queryPlanRequests);
    const double queryCacheHitPercent =
        queryPlanRequests == 0.0 ? 0.0 : (static_cast<double>(telemetryState_->counters.queryCacheHits) * 100.0) / queryPlanRequests;
    const double queryCacheMissPercent =
        queryPlanRequests == 0.0 ? 0.0 : (static_cast<double>(telemetryState_->counters.queryCacheMisses) * 100.0) / queryPlanRequests;
    const double queryPlanBuilds = static_cast<double>(telemetryState_->counters.queryPlanBuilds);
    const double queryAveragePlanCacheLookupNanoseconds = queryPlanRequests == 0.0
        ? 0.0
        : static_cast<double>(telemetryState_->counters.queryPlanCacheLookupElapsedNanoseconds) / queryPlanRequests;
    const double queryAveragePlanBuildNanoseconds = queryPlanBuilds == 0.0
        ? 0.0
        : static_cast<double>(telemetryState_->counters.queryPlanBuildElapsedNanoseconds) / queryPlanBuilds;
    const double queryRecordCacheRequests =
        static_cast<double>(telemetryState_->counters.queryRecordCacheHits + telemetryState_->counters.queryRecordCacheMisses);
    const double queryRecordCacheHitPercent = queryRecordCacheRequests == 0.0
        ? 0.0
        : (static_cast<double>(telemetryState_->counters.queryRecordCacheHits) * 100.0) / queryRecordCacheRequests;
    const double queryRecordCacheMissPercent = queryRecordCacheRequests == 0.0
        ? 0.0
        : (static_cast<double>(telemetryState_->counters.queryRecordCacheMisses) * 100.0) / queryRecordCacheRequests;
    const double queryWorkerSlots = static_cast<double>(telemetryState_->counters.queryWorkerSlots);
    const double queryWorkerUtilizationPercent =
        queryWorkerSlots == 0.0 ? 0.0 : (static_cast<double>(telemetryState_->counters.queryWorkerActiveSlots) * 100.0) / queryWorkerSlots;
    const MemoryBandwidthSample queryBandwidth =
        EstimateMemoryBandwidth(telemetryState_->counters.queryBytesTouched, telemetryState_->counters.queryElapsedNanoseconds);
    const MemoryBandwidthSample queryKernelBandwidth =
        EstimateMemoryBandwidth(telemetryState_->counters.queryBytesTouched, telemetryState_->counters.queryKernelElapsedNanoseconds);
    const double queryEstimatedBytesPerSecond = queryBandwidth.BytesPerSecond();
    const double queryEstimatedGigabytesPerSecond = queryBandwidth.GigabytesPerSecond();
    const double queryKernelEstimatedBytesPerSecond = queryKernelBandwidth.BytesPerSecond();
    const double queryKernelEstimatedGigabytesPerSecond = queryKernelBandwidth.GigabytesPerSecond();
    const double queryExecutions = static_cast<double>(telemetryState_->counters.queryExecutions);
    const double queryEntitiesVisited = static_cast<double>(telemetryState_->counters.queryEntitiesVisited);
    const double queryAverageBytesPerEntity =
        queryEntitiesVisited == 0.0 ? 0.0 : static_cast<double>(telemetryState_->counters.queryBytesTouched) / queryEntitiesVisited;
    const double queryAveragePrefetchDistance =
        queryExecutions == 0.0 ? 0.0 : static_cast<double>(telemetryState_->counters.queryPrefetchDistanceTotal) / queryExecutions;
    const double queryPrepareCalls = static_cast<double>(telemetryState_->counters.queryPrepareCalls);
    const double queryAveragePrepareNanoseconds =
        queryPrepareCalls == 0.0 ? 0.0 : static_cast<double>(telemetryState_->counters.queryPrepareElapsedNanoseconds) / queryPrepareCalls;
    const double queryAverageMatchedChunks =
        queryPrepareCalls == 0.0 ? 0.0 : static_cast<double>(telemetryState_->counters.queryMatchedChunks) / queryPrepareCalls;
    const double queryAverageMatchedArchetypes =
        queryPrepareCalls == 0.0 ? 0.0 : static_cast<double>(telemetryState_->counters.queryMatchedArchetypes) / queryPrepareCalls;
    const double queryAverageKernelNanoseconds =
        queryExecutions == 0.0 ? 0.0 : static_cast<double>(telemetryState_->counters.queryKernelElapsedNanoseconds) / queryExecutions;
    const double queryAverageEffectiveBatchSize =
        queryExecutions == 0.0 ? 0.0 : static_cast<double>(telemetryState_->counters.queryEffectiveBatchSizeTotal) / queryExecutions;
    const KernelBackendReport preferredKernelReport = PreferredKernelBackendReport();
    const KernelBackendReport avx2KernelReport = MakeKernelBackendReport(KernelBackend::Avx2);

    const std::size_t activePayloadBytes = storageStats.activePayloadBytes;
    const std::size_t committedPayloadBytes = storageStats.committedPayloadBytes;
    const std::size_t storageSystemAllocationsSinceReset =
        storageStats.chunkPoolSystemAllocationCount >= telemetryState_->counters.storageSystemAllocationCountAtReset
            ? storageStats.chunkPoolSystemAllocationCount - telemetryState_->counters.storageSystemAllocationCountAtReset
            : 0U;
    return WorldTelemetrySnapshot{
        .entityCount = storageStats.liveEntities,
        .archetypeCount = storageStats.archetypeCount,
        .chunkSizeProfile = std::string{ ChunkSizeProfileName(config_.chunkSizeProfile) },
        .chunkPayloadBytes = NativeChunkPayloadBytes(),
        .chunkCount = storageStats.chunks,
        .chunkCapacity = capacity,
        .hotOnlyChunkCapacity = storageStats.hotOnlyCapacity,
        .capacityLostToNonHotStorage = storageStats.capacityLostToNonHotStorage,
        .sparseChunkCount = storageStats.sparseChunks,
        .tailSparseChunkCount = storageStats.tailSparseChunks,
        .fragmentedChunkCount = storageStats.fragmentedChunks,
        .emptyChunkCount = storageStats.emptyChunks,
        .chunkPoolAllocated = storageStats.chunkPoolAllocated,
        .chunkPoolInUse = storageStats.chunkPoolInUse,
        .chunkPoolFree = storageStats.chunkPoolFree,
        .chunkPoolAcquireCount = storageStats.chunkPoolAcquireCount,
        .chunkPoolReuseCount = storageStats.chunkPoolReuseCount,
        .chunkPoolReleaseCount = storageStats.chunkPoolReleaseCount,
        .chunkPoolTrimCount = storageStats.chunkPoolTrimCount,
        .chunkPoolSystemAllocationCount = storageStats.chunkPoolSystemAllocationCount,
        .chunkPoolPeakAllocated = storageStats.chunkPoolPeakAllocated,
        .bytesPerEntity = storageStats.liveEntities == 0 ? 0 : storageStats.usedBytes / storageStats.liveEntities,
        .allocatedBytes = activePayloadBytes,
        .sidePayloadBytes = storageStats.activeSidePayloadBytes,
        .committedBytes = committedPayloadBytes,
        .freeBytes = storageStats.freePayloadBytes,
        .peakCommittedBytes = storageStats.peakCommittedPayloadBytes,
        .chunkMetadataBytes = storageStats.chunkMetadataBytes,
        .entityRecordBytes = storageStats.entityRecordBytes,
        .trackedBytes = storageStats.trackedBytes,
        .usedBytes = storageStats.usedBytes,
        .wastedBytes = storageStats.wastedBytes,
        .hotTableComponents = storageStats.hotTableComponents,
        .coldTableComponents = storageStats.coldTableComponents,
        .sparseTagComponents = storageStats.sparseTagComponents,
        .sparsePayloadComponents = storageStats.sparsePayloadComponents,
        .sharedValueComponents = storageStats.sharedValueComponents,
        .externalBlobComponents = storageStats.externalBlobComponents,
        .hotTableUsedBytes = storageStats.hotTableUsedBytes,
        .coldTableUsedBytes = storageStats.coldTableUsedBytes,
        .sparseTagUsedBytes = storageStats.sparseTagUsedBytes,
        .sparsePayloadUsedBytes = storageStats.sparsePayloadUsedBytes,
        .sharedValueUsedBytes = storageStats.sharedValueUsedBytes,
        .externalBlobUsedBytes = storageStats.externalBlobUsedBytes,
        .hotTableCapacityBytes = storageStats.hotTableCapacityBytes,
        .coldTableCapacityBytes = storageStats.coldTableCapacityBytes,
        .sparseTagCapacityBytes = storageStats.sparseTagCapacityBytes,
        .sparsePayloadCapacityBytes = storageStats.sparsePayloadCapacityBytes,
        .sharedValueCapacityBytes = storageStats.sharedValueCapacityBytes,
        .externalBlobCapacityBytes = storageStats.externalBlobCapacityBytes,
        .storageSystemAllocationCount = storageStats.chunkPoolSystemAllocationCount,
        .storageSystemAllocationsSinceReset = storageSystemAllocationsSinceReset,
        .occupancyPercent = occupancyPercent,
        .fragmentationPercent = capacity == 0 ? 0.0 : 100.0 - occupancyPercent,
        .sparseChunkPercent = sparseChunkPercent,
        .tailSparseChunkPercent = tailSparseChunkPercent,
        .fragmentedChunkPercent = fragmentedChunkPercent,
        .emptyChunkPercent = emptyChunkPercent,
        .queryPlanRequests = telemetryState_->counters.queryPlanRequests,
        .queryCacheHits = telemetryState_->counters.queryCacheHits,
        .queryCacheMisses = telemetryState_->counters.queryCacheMisses,
        .queryPlanBuilds = telemetryState_->counters.queryPlanBuilds,
        .queryPlanCacheLookupElapsedNanoseconds = telemetryState_->counters.queryPlanCacheLookupElapsedNanoseconds,
        .queryPlanBuildElapsedNanoseconds = telemetryState_->counters.queryPlanBuildElapsedNanoseconds,
        .queryRecordCacheHits = telemetryState_->counters.queryRecordCacheHits,
        .queryRecordCacheMisses = telemetryState_->counters.queryRecordCacheMisses,
        .queryCacheHitPercent = queryCacheHitPercent,
        .queryCacheMissPercent = queryCacheMissPercent,
        .queryAveragePlanCacheLookupNanoseconds = queryAveragePlanCacheLookupNanoseconds,
        .queryAveragePlanBuildNanoseconds = queryAveragePlanBuildNanoseconds,
        .queryRecordCacheHitPercent = queryRecordCacheHitPercent,
        .queryRecordCacheMissPercent = queryRecordCacheMissPercent,
        .queryExecutions = telemetryState_->counters.queryExecutions,
        .queryBatches = telemetryState_->counters.queryBatches,
        .queryEntitiesVisited = telemetryState_->counters.queryEntitiesVisited,
        .queryBytesTouched = telemetryState_->counters.queryBytesTouched,
        .queryElapsedNanoseconds = telemetryState_->counters.queryElapsedNanoseconds,
        .queryPrepareCalls = telemetryState_->counters.queryPrepareCalls,
        .queryPrepareRecords = telemetryState_->counters.queryPrepareRecords,
        .queryMatchedChunks = telemetryState_->counters.queryMatchedChunks,
        .queryMatchedArchetypes = telemetryState_->counters.queryMatchedArchetypes,
        .queryPrepareElapsedNanoseconds = telemetryState_->counters.queryPrepareElapsedNanoseconds,
        .queryKernelElapsedNanoseconds = telemetryState_->counters.queryKernelElapsedNanoseconds,
        .queryAdaptiveExecutions = telemetryState_->counters.queryAdaptiveExecutions,
        .queryEffectiveBatchSizeTotal = telemetryState_->counters.queryEffectiveBatchSizeTotal,
        .queryMaxEffectiveBatchSize = telemetryState_->counters.queryMaxEffectiveBatchSize,
        .querySingleThreadExecutions = telemetryState_->counters.querySingleThreadExecutions,
        .queryParallelChunkExecutions = telemetryState_->counters.queryParallelChunkExecutions,
        .queryParallelRangeExecutions = telemetryState_->counters.queryParallelRangeExecutions,
        .querySimdPreferredExecutions = telemetryState_->counters.querySimdPreferredExecutions,
        .queryDeterministicExecutions = telemetryState_->counters.queryDeterministicExecutions,
        .queryEstimatedBytesPerSecond = queryEstimatedBytesPerSecond,
        .queryEstimatedGigabytesPerSecond = queryEstimatedGigabytesPerSecond,
        .queryKernelEstimatedBytesPerSecond = queryKernelEstimatedBytesPerSecond,
        .queryKernelEstimatedGigabytesPerSecond = queryKernelEstimatedGigabytesPerSecond,
        .queryAverageBytesPerEntity = queryAverageBytesPerEntity,
        .queryAveragePrepareNanoseconds = queryAveragePrepareNanoseconds,
        .queryAverageMatchedChunks = queryAverageMatchedChunks,
        .queryAverageMatchedArchetypes = queryAverageMatchedArchetypes,
        .queryAverageKernelNanoseconds = queryAverageKernelNanoseconds,
        .queryAverageEffectiveBatchSize = queryAverageEffectiveBatchSize,
        .queryPrefetchDistanceTotal = telemetryState_->counters.queryPrefetchDistanceTotal,
        .queryAveragePrefetchDistance = queryAveragePrefetchDistance,
        .queryParallelExecutions = telemetryState_->counters.queryParallelExecutions,
        .queryWorkerSlots = telemetryState_->counters.queryWorkerSlots,
        .queryWorkerActiveSlots = telemetryState_->counters.queryWorkerActiveSlots,
        .queryWorkerUtilizationPercent = queryWorkerUtilizationPercent,
        .preferredKernelBackend = std::string{ preferredKernelReport.name },
        .preferredKernelFloatLanes = preferredKernelReport.floatLanes,
        .preferredKernelBackendCompiled = preferredKernelReport.nativelyCompiled,
        .preferredKernelBackendSupported = preferredKernelReport.hardwareSupported,
        .preferredKernelBackendAutoSelectable = preferredKernelReport.autoSelectable,
        .avx2KernelBackendCompiled = avx2KernelReport.nativelyCompiled,
        .avx2KernelBackendSupported = avx2KernelReport.hardwareSupported,
        .avx2KernelBackendAutoSelectable = avx2KernelReport.autoSelectable,
        .compatMutableIterations = telemetryState_->counters.compatMutableIterations,
        .compatMutableEntitiesVisited = telemetryState_->counters.compatMutableEntitiesVisited,
        .structuralChangesSinceReset = telemetryState_->counters.structuralChangesSinceReset,
        .totalStructuralChanges = telemetryState_->counters.totalStructuralChanges,
        .archetypeTransitionInvalidationsSinceReset = telemetryState_->counters.archetypeTransitionInvalidationsSinceReset,
        .totalArchetypeTransitionInvalidations = telemetryState_->counters.totalArchetypeTransitionInvalidations,
    };
}

void World::ResetTelemetryFrameCounters() noexcept {
    const std::lock_guard lock(telemetryState_->mutex);
    const NativeEcsStorageStats storageStats = NativeStorageStats();
    telemetryState_->counters.storageSystemAllocationCountAtReset = storageStats.chunkPoolSystemAllocationCount;
    telemetryState_->counters.structuralChangesSinceReset = 0;
    telemetryState_->counters.queryExecutions = 0;
    telemetryState_->counters.queryBatches = 0;
    telemetryState_->counters.queryEntitiesVisited = 0;
    telemetryState_->counters.queryBytesTouched = 0;
    telemetryState_->counters.queryElapsedNanoseconds = 0;
    telemetryState_->counters.queryPrepareCalls = 0;
    telemetryState_->counters.queryPrepareRecords = 0;
    telemetryState_->counters.queryMatchedChunks = 0;
    telemetryState_->counters.queryMatchedArchetypes = 0;
    telemetryState_->counters.queryPrepareElapsedNanoseconds = 0;
    telemetryState_->counters.queryKernelElapsedNanoseconds = 0;
    telemetryState_->counters.queryRecordCacheHits = 0;
    telemetryState_->counters.queryRecordCacheMisses = 0;
    telemetryState_->counters.queryAdaptiveExecutions = 0;
    telemetryState_->counters.queryEffectiveBatchSizeTotal = 0;
    telemetryState_->counters.queryMaxEffectiveBatchSize = 0;
    telemetryState_->counters.querySingleThreadExecutions = 0;
    telemetryState_->counters.queryParallelChunkExecutions = 0;
    telemetryState_->counters.queryParallelRangeExecutions = 0;
    telemetryState_->counters.querySimdPreferredExecutions = 0;
    telemetryState_->counters.queryDeterministicExecutions = 0;
    telemetryState_->counters.queryPrefetchDistanceTotal = 0;
    telemetryState_->counters.queryParallelExecutions = 0;
    telemetryState_->counters.queryWorkerSlots = 0;
    telemetryState_->counters.queryWorkerActiveSlots = 0;
    telemetryState_->counters.compatMutableIterations = 0;
    telemetryState_->counters.compatMutableEntitiesVisited = 0;
    telemetryState_->counters.archetypeTransitionInvalidationsSinceReset = 0;
}

} // namespace kb::ecs
