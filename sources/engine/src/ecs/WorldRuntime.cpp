#include "engine/ecs/World.hpp"

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

WorldTelemetrySnapshot World::TelemetrySnapshot() const {
    const NativeEcsStorageStats storageStats = NativeStorageStats();
    const std::size_t capacity = storageStats.capacity;
    const double occupancyPercent = capacity == 0 ? 0.0 : (static_cast<double>(storageStats.liveEntities) * 100.0) / static_cast<double>(capacity);
    const double queryPlanRequests = static_cast<double>(telemetryCounters_.queryPlanRequests);
    const double queryCacheHitPercent =
        queryPlanRequests == 0.0 ? 0.0 : (static_cast<double>(telemetryCounters_.queryCacheHits) * 100.0) / queryPlanRequests;
    const double queryCacheMissPercent =
        queryPlanRequests == 0.0 ? 0.0 : (static_cast<double>(telemetryCounters_.queryCacheMisses) * 100.0) / queryPlanRequests;
    const double queryWorkerSlots = static_cast<double>(telemetryCounters_.queryWorkerSlots);
    const double queryWorkerUtilizationPercent =
        queryWorkerSlots == 0.0 ? 0.0 : (static_cast<double>(telemetryCounters_.queryWorkerActiveSlots) * 100.0) / queryWorkerSlots;
    const double queryElapsedSeconds = static_cast<double>(telemetryCounters_.queryElapsedNanoseconds) / 1'000'000'000.0;
    const double queryEstimatedBytesPerSecond =
        queryElapsedSeconds == 0.0 ? 0.0 : static_cast<double>(telemetryCounters_.queryBytesTouched) / queryElapsedSeconds;

    const std::size_t allocatedBytes = storageStats.chunks * NativeChunkPayloadBytes();
    return WorldTelemetrySnapshot{
        .entityCount = storageStats.liveEntities,
        .archetypeCount = storageStats.archetypeCount,
        .chunkCount = storageStats.chunks,
        .chunkCapacity = capacity,
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
        .bytesPerEntity = storageStats.liveEntities == 0 ? 0 : storageStats.usedBytes / storageStats.liveEntities,
        .allocatedBytes = allocatedBytes,
        .usedBytes = storageStats.usedBytes,
        .wastedBytes = storageStats.wastedBytes,
        .occupancyPercent = occupancyPercent,
        .fragmentationPercent = capacity == 0 ? 0.0 : 100.0 - occupancyPercent,
        .queryPlanRequests = telemetryCounters_.queryPlanRequests,
        .queryCacheHits = telemetryCounters_.queryCacheHits,
        .queryCacheMisses = telemetryCounters_.queryCacheMisses,
        .queryCacheHitPercent = queryCacheHitPercent,
        .queryCacheMissPercent = queryCacheMissPercent,
        .queryExecutions = telemetryCounters_.queryExecutions,
        .queryBatches = telemetryCounters_.queryBatches,
        .queryEntitiesVisited = telemetryCounters_.queryEntitiesVisited,
        .queryBytesTouched = telemetryCounters_.queryBytesTouched,
        .queryElapsedNanoseconds = telemetryCounters_.queryElapsedNanoseconds,
        .queryEstimatedBytesPerSecond = queryEstimatedBytesPerSecond,
        .queryParallelExecutions = telemetryCounters_.queryParallelExecutions,
        .queryWorkerSlots = telemetryCounters_.queryWorkerSlots,
        .queryWorkerActiveSlots = telemetryCounters_.queryWorkerActiveSlots,
        .queryWorkerUtilizationPercent = queryWorkerUtilizationPercent,
        .compatMutableIterations = telemetryCounters_.compatMutableIterations,
        .compatMutableEntitiesVisited = telemetryCounters_.compatMutableEntitiesVisited,
        .structuralChangesSinceReset = telemetryCounters_.structuralChangesSinceReset,
        .totalStructuralChanges = telemetryCounters_.totalStructuralChanges,
    };
}

void World::ResetTelemetryFrameCounters() noexcept {
    telemetryCounters_.structuralChangesSinceReset = 0;
    telemetryCounters_.queryExecutions = 0;
    telemetryCounters_.queryBatches = 0;
    telemetryCounters_.queryEntitiesVisited = 0;
    telemetryCounters_.queryBytesTouched = 0;
    telemetryCounters_.queryElapsedNanoseconds = 0;
    telemetryCounters_.queryParallelExecutions = 0;
    telemetryCounters_.queryWorkerSlots = 0;
    telemetryCounters_.queryWorkerActiveSlots = 0;
    telemetryCounters_.compatMutableIterations = 0;
    telemetryCounters_.compatMutableEntitiesVisited = 0;
}

} // namespace kb::ecs
