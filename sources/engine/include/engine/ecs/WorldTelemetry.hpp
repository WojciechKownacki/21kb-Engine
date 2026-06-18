#pragma once

#include <cstddef>
#include <cstdint>

namespace kb::ecs {

struct WorldTelemetrySnapshot {
    std::size_t entityCount = 0;
    std::size_t archetypeCount = 0;
    std::size_t chunkCount = 0;
    std::size_t chunkCapacity = 0;
    std::size_t sparseChunkCount = 0;
    std::size_t tailSparseChunkCount = 0;
    std::size_t fragmentedChunkCount = 0;
    std::size_t emptyChunkCount = 0;
    std::size_t chunkPoolAllocated = 0;
    std::size_t chunkPoolInUse = 0;
    std::size_t chunkPoolFree = 0;
    std::size_t chunkPoolAcquireCount = 0;
    std::size_t chunkPoolReuseCount = 0;
    std::size_t chunkPoolReleaseCount = 0;
    std::size_t chunkPoolTrimCount = 0;
    std::size_t bytesPerEntity = 0;
    std::size_t allocatedBytes = 0;
    std::size_t usedBytes = 0;
    std::size_t wastedBytes = 0;
    double occupancyPercent = 0.0;
    double fragmentationPercent = 0.0;
    std::uint64_t queryPlanRequests = 0;
    std::uint64_t queryCacheHits = 0;
    std::uint64_t queryCacheMisses = 0;
    double queryCacheHitPercent = 0.0;
    double queryCacheMissPercent = 0.0;
    std::uint64_t queryExecutions = 0;
    std::uint64_t queryBatches = 0;
    std::uint64_t queryEntitiesVisited = 0;
    std::uint64_t queryBytesTouched = 0;
    std::uint64_t queryElapsedNanoseconds = 0;
    double queryEstimatedBytesPerSecond = 0.0;
    std::uint64_t queryParallelExecutions = 0;
    std::uint64_t queryWorkerSlots = 0;
    std::uint64_t queryWorkerActiveSlots = 0;
    double queryWorkerUtilizationPercent = 0.0;
    std::uint64_t compatMutableIterations = 0;
    std::uint64_t compatMutableEntitiesVisited = 0;
    std::uint64_t structuralChangesSinceReset = 0;
    std::uint64_t totalStructuralChanges = 0;
};

struct WorldTelemetryCounters {
    std::uint64_t queryPlanRequests = 0;
    std::uint64_t queryCacheHits = 0;
    std::uint64_t queryCacheMisses = 0;
    std::uint64_t queryExecutions = 0;
    std::uint64_t queryBatches = 0;
    std::uint64_t queryEntitiesVisited = 0;
    std::uint64_t queryBytesTouched = 0;
    std::uint64_t queryElapsedNanoseconds = 0;
    std::uint64_t queryParallelExecutions = 0;
    std::uint64_t queryWorkerSlots = 0;
    std::uint64_t queryWorkerActiveSlots = 0;
    std::uint64_t compatMutableIterations = 0;
    std::uint64_t compatMutableEntitiesVisited = 0;
    std::uint64_t structuralChangesSinceReset = 0;
    std::uint64_t totalStructuralChanges = 0;
};

} // namespace kb::ecs
