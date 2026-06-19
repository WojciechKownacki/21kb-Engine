#include "engine/ecs/World.hpp"

#include "engine/ecs/Kernel.hpp"
#include "engine/ecs/MemoryTrafficEstimator.hpp"

#include "ecs/FlecsEntityIds.hpp"

#include <flecs.h>

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
    const double queryPlanRequests = static_cast<double>(telemetryCounters_.queryPlanRequests);
    const double queryCacheHitPercent =
        queryPlanRequests == 0.0 ? 0.0 : (static_cast<double>(telemetryCounters_.queryCacheHits) * 100.0) / queryPlanRequests;
    const double queryCacheMissPercent =
        queryPlanRequests == 0.0 ? 0.0 : (static_cast<double>(telemetryCounters_.queryCacheMisses) * 100.0) / queryPlanRequests;
    const double queryPlanBuilds = static_cast<double>(telemetryCounters_.queryPlanBuilds);
    const double queryAveragePlanCacheLookupNanoseconds = queryPlanRequests == 0.0
        ? 0.0
        : static_cast<double>(telemetryCounters_.queryPlanCacheLookupElapsedNanoseconds) / queryPlanRequests;
    const double queryAveragePlanBuildNanoseconds = queryPlanBuilds == 0.0
        ? 0.0
        : static_cast<double>(telemetryCounters_.queryPlanBuildElapsedNanoseconds) / queryPlanBuilds;
    const double queryRecordCacheRequests =
        static_cast<double>(telemetryCounters_.queryRecordCacheHits + telemetryCounters_.queryRecordCacheMisses);
    const double queryRecordCacheHitPercent = queryRecordCacheRequests == 0.0
        ? 0.0
        : (static_cast<double>(telemetryCounters_.queryRecordCacheHits) * 100.0) / queryRecordCacheRequests;
    const double queryRecordCacheMissPercent = queryRecordCacheRequests == 0.0
        ? 0.0
        : (static_cast<double>(telemetryCounters_.queryRecordCacheMisses) * 100.0) / queryRecordCacheRequests;
    const double queryWorkerSlots = static_cast<double>(telemetryCounters_.queryWorkerSlots);
    const double queryWorkerUtilizationPercent =
        queryWorkerSlots == 0.0 ? 0.0 : (static_cast<double>(telemetryCounters_.queryWorkerActiveSlots) * 100.0) / queryWorkerSlots;
    const MemoryBandwidthSample queryBandwidth =
        EstimateMemoryBandwidth(telemetryCounters_.queryBytesTouched, telemetryCounters_.queryElapsedNanoseconds);
    const MemoryBandwidthSample queryKernelBandwidth =
        EstimateMemoryBandwidth(telemetryCounters_.queryBytesTouched, telemetryCounters_.queryKernelElapsedNanoseconds);
    const double queryEstimatedBytesPerSecond = queryBandwidth.BytesPerSecond();
    const double queryEstimatedGigabytesPerSecond = queryBandwidth.GigabytesPerSecond();
    const double queryKernelEstimatedBytesPerSecond = queryKernelBandwidth.BytesPerSecond();
    const double queryKernelEstimatedGigabytesPerSecond = queryKernelBandwidth.GigabytesPerSecond();
    const double queryExecutions = static_cast<double>(telemetryCounters_.queryExecutions);
    const double queryEntitiesVisited = static_cast<double>(telemetryCounters_.queryEntitiesVisited);
    const double queryAverageBytesPerEntity =
        queryEntitiesVisited == 0.0 ? 0.0 : static_cast<double>(telemetryCounters_.queryBytesTouched) / queryEntitiesVisited;
    const double queryAveragePrefetchDistance =
        queryExecutions == 0.0 ? 0.0 : static_cast<double>(telemetryCounters_.queryPrefetchDistanceTotal) / queryExecutions;
    const double queryPrepareCalls = static_cast<double>(telemetryCounters_.queryPrepareCalls);
    const double queryAveragePrepareNanoseconds =
        queryPrepareCalls == 0.0 ? 0.0 : static_cast<double>(telemetryCounters_.queryPrepareElapsedNanoseconds) / queryPrepareCalls;
    const double queryAverageMatchedChunks =
        queryPrepareCalls == 0.0 ? 0.0 : static_cast<double>(telemetryCounters_.queryMatchedChunks) / queryPrepareCalls;
    const double queryAverageMatchedArchetypes =
        queryPrepareCalls == 0.0 ? 0.0 : static_cast<double>(telemetryCounters_.queryMatchedArchetypes) / queryPrepareCalls;
    const double queryAverageKernelNanoseconds =
        queryExecutions == 0.0 ? 0.0 : static_cast<double>(telemetryCounters_.queryKernelElapsedNanoseconds) / queryExecutions;
    const double queryAverageEffectiveBatchSize =
        queryExecutions == 0.0 ? 0.0 : static_cast<double>(telemetryCounters_.queryEffectiveBatchSizeTotal) / queryExecutions;
    const KernelBackendReport preferredKernelReport = PreferredKernelBackendReport();
    const KernelBackendReport avx2KernelReport = MakeKernelBackendReport(KernelBackend::Avx2);

    const std::size_t activePayloadBytes = storageStats.activePayloadBytes;
    const std::size_t committedPayloadBytes = storageStats.committedPayloadBytes;
    const std::size_t storageSystemAllocationsSinceReset =
        storageStats.chunkPoolSystemAllocationCount >= telemetryCounters_.storageSystemAllocationCountAtReset
            ? storageStats.chunkPoolSystemAllocationCount - telemetryCounters_.storageSystemAllocationCountAtReset
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
        .queryPlanRequests = telemetryCounters_.queryPlanRequests,
        .queryCacheHits = telemetryCounters_.queryCacheHits,
        .queryCacheMisses = telemetryCounters_.queryCacheMisses,
        .queryPlanBuilds = telemetryCounters_.queryPlanBuilds,
        .queryPlanCacheLookupElapsedNanoseconds = telemetryCounters_.queryPlanCacheLookupElapsedNanoseconds,
        .queryPlanBuildElapsedNanoseconds = telemetryCounters_.queryPlanBuildElapsedNanoseconds,
        .queryRecordCacheHits = telemetryCounters_.queryRecordCacheHits,
        .queryRecordCacheMisses = telemetryCounters_.queryRecordCacheMisses,
        .queryCacheHitPercent = queryCacheHitPercent,
        .queryCacheMissPercent = queryCacheMissPercent,
        .queryAveragePlanCacheLookupNanoseconds = queryAveragePlanCacheLookupNanoseconds,
        .queryAveragePlanBuildNanoseconds = queryAveragePlanBuildNanoseconds,
        .queryRecordCacheHitPercent = queryRecordCacheHitPercent,
        .queryRecordCacheMissPercent = queryRecordCacheMissPercent,
        .queryExecutions = telemetryCounters_.queryExecutions,
        .queryBatches = telemetryCounters_.queryBatches,
        .queryEntitiesVisited = telemetryCounters_.queryEntitiesVisited,
        .queryBytesTouched = telemetryCounters_.queryBytesTouched,
        .queryElapsedNanoseconds = telemetryCounters_.queryElapsedNanoseconds,
        .queryPrepareCalls = telemetryCounters_.queryPrepareCalls,
        .queryPrepareRecords = telemetryCounters_.queryPrepareRecords,
        .queryMatchedChunks = telemetryCounters_.queryMatchedChunks,
        .queryMatchedArchetypes = telemetryCounters_.queryMatchedArchetypes,
        .queryPrepareElapsedNanoseconds = telemetryCounters_.queryPrepareElapsedNanoseconds,
        .queryKernelElapsedNanoseconds = telemetryCounters_.queryKernelElapsedNanoseconds,
        .queryAdaptiveExecutions = telemetryCounters_.queryAdaptiveExecutions,
        .queryEffectiveBatchSizeTotal = telemetryCounters_.queryEffectiveBatchSizeTotal,
        .queryMaxEffectiveBatchSize = telemetryCounters_.queryMaxEffectiveBatchSize,
        .querySingleThreadExecutions = telemetryCounters_.querySingleThreadExecutions,
        .queryParallelChunkExecutions = telemetryCounters_.queryParallelChunkExecutions,
        .queryParallelRangeExecutions = telemetryCounters_.queryParallelRangeExecutions,
        .querySimdPreferredExecutions = telemetryCounters_.querySimdPreferredExecutions,
        .queryDeterministicExecutions = telemetryCounters_.queryDeterministicExecutions,
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
        .queryPrefetchDistanceTotal = telemetryCounters_.queryPrefetchDistanceTotal,
        .queryAveragePrefetchDistance = queryAveragePrefetchDistance,
        .queryParallelExecutions = telemetryCounters_.queryParallelExecutions,
        .queryWorkerSlots = telemetryCounters_.queryWorkerSlots,
        .queryWorkerActiveSlots = telemetryCounters_.queryWorkerActiveSlots,
        .queryWorkerUtilizationPercent = queryWorkerUtilizationPercent,
        .preferredKernelBackend = std::string{ preferredKernelReport.name },
        .preferredKernelFloatLanes = preferredKernelReport.floatLanes,
        .preferredKernelBackendCompiled = preferredKernelReport.nativelyCompiled,
        .preferredKernelBackendSupported = preferredKernelReport.hardwareSupported,
        .preferredKernelBackendAutoSelectable = preferredKernelReport.autoSelectable,
        .avx2KernelBackendCompiled = avx2KernelReport.nativelyCompiled,
        .avx2KernelBackendSupported = avx2KernelReport.hardwareSupported,
        .avx2KernelBackendAutoSelectable = avx2KernelReport.autoSelectable,
        .compatMutableIterations = telemetryCounters_.compatMutableIterations,
        .compatMutableEntitiesVisited = telemetryCounters_.compatMutableEntitiesVisited,
        .structuralChangesSinceReset = telemetryCounters_.structuralChangesSinceReset,
        .totalStructuralChanges = telemetryCounters_.totalStructuralChanges,
        .archetypeTransitionInvalidationsSinceReset = telemetryCounters_.archetypeTransitionInvalidationsSinceReset,
        .totalArchetypeTransitionInvalidations = telemetryCounters_.totalArchetypeTransitionInvalidations,
    };
}

void World::ResetTelemetryFrameCounters() noexcept {
    const NativeEcsStorageStats storageStats = NativeStorageStats();
    telemetryCounters_.storageSystemAllocationCountAtReset = storageStats.chunkPoolSystemAllocationCount;
    telemetryCounters_.structuralChangesSinceReset = 0;
    telemetryCounters_.queryExecutions = 0;
    telemetryCounters_.queryBatches = 0;
    telemetryCounters_.queryEntitiesVisited = 0;
    telemetryCounters_.queryBytesTouched = 0;
    telemetryCounters_.queryElapsedNanoseconds = 0;
    telemetryCounters_.queryPrepareCalls = 0;
    telemetryCounters_.queryPrepareRecords = 0;
    telemetryCounters_.queryMatchedChunks = 0;
    telemetryCounters_.queryMatchedArchetypes = 0;
    telemetryCounters_.queryPrepareElapsedNanoseconds = 0;
    telemetryCounters_.queryKernelElapsedNanoseconds = 0;
    telemetryCounters_.queryRecordCacheHits = 0;
    telemetryCounters_.queryRecordCacheMisses = 0;
    telemetryCounters_.queryAdaptiveExecutions = 0;
    telemetryCounters_.queryEffectiveBatchSizeTotal = 0;
    telemetryCounters_.queryMaxEffectiveBatchSize = 0;
    telemetryCounters_.querySingleThreadExecutions = 0;
    telemetryCounters_.queryParallelChunkExecutions = 0;
    telemetryCounters_.queryParallelRangeExecutions = 0;
    telemetryCounters_.querySimdPreferredExecutions = 0;
    telemetryCounters_.queryDeterministicExecutions = 0;
    telemetryCounters_.queryPrefetchDistanceTotal = 0;
    telemetryCounters_.queryParallelExecutions = 0;
    telemetryCounters_.queryWorkerSlots = 0;
    telemetryCounters_.queryWorkerActiveSlots = 0;
    telemetryCounters_.compatMutableIterations = 0;
    telemetryCounters_.compatMutableEntitiesVisited = 0;
    telemetryCounters_.archetypeTransitionInvalidationsSinceReset = 0;
}

} // namespace kb::ecs
