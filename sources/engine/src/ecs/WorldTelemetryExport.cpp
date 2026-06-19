#include "engine/ecs/WorldTelemetryExport.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace kb::ecs {
namespace {

void WriteFieldPrefix(std::ostream& output, const char* name, bool& first) {
    if (!first) {
        output << ",\n";
    }
    first = false;
    output << "  \"" << name << "\": ";
}

template <typename T>
void WriteNumberField(std::ostream& output, const char* name, T value, bool& first) {
    WriteFieldPrefix(output, name, first);
    output << value;
}

void WriteBoolField(std::ostream& output, const char* name, bool value, bool& first) {
    WriteFieldPrefix(output, name, first);
    output << (value ? "true" : "false");
}

void WriteStringField(std::ostream& output, const char* name, std::string_view value, bool& first) {
    WriteFieldPrefix(output, name, first);
    output << '"' << value << '"';
}

} // namespace

std::string WorldTelemetrySnapshotToJson(const WorldTelemetrySnapshot& snapshot) {
    std::ostringstream output;
    output << std::setprecision(17);
    output << "{\n";
    output << "  \"schema\": \"kb.ecs.world_telemetry.v1\"";
    bool first = false;

    WriteNumberField(output, "entity_count", snapshot.entityCount, first);
    WriteNumberField(output, "archetype_count", snapshot.archetypeCount, first);
    WriteStringField(output, "chunk_size_profile", snapshot.chunkSizeProfile, first);
    WriteNumberField(output, "chunk_payload_bytes", snapshot.chunkPayloadBytes, first);
    WriteNumberField(output, "chunk_count", snapshot.chunkCount, first);
    WriteNumberField(output, "chunk_capacity", snapshot.chunkCapacity, first);
    WriteNumberField(output, "hot_only_chunk_capacity", snapshot.hotOnlyChunkCapacity, first);
    WriteNumberField(output, "capacity_lost_to_non_hot_storage", snapshot.capacityLostToNonHotStorage, first);
    WriteNumberField(output, "sparse_chunk_count", snapshot.sparseChunkCount, first);
    WriteNumberField(output, "tail_sparse_chunk_count", snapshot.tailSparseChunkCount, first);
    WriteNumberField(output, "fragmented_chunk_count", snapshot.fragmentedChunkCount, first);
    WriteNumberField(output, "empty_chunk_count", snapshot.emptyChunkCount, first);
    WriteNumberField(output, "chunk_pool_allocated", snapshot.chunkPoolAllocated, first);
    WriteNumberField(output, "chunk_pool_in_use", snapshot.chunkPoolInUse, first);
    WriteNumberField(output, "chunk_pool_free", snapshot.chunkPoolFree, first);
    WriteNumberField(output, "chunk_pool_acquire_count", snapshot.chunkPoolAcquireCount, first);
    WriteNumberField(output, "chunk_pool_reuse_count", snapshot.chunkPoolReuseCount, first);
    WriteNumberField(output, "chunk_pool_release_count", snapshot.chunkPoolReleaseCount, first);
    WriteNumberField(output, "chunk_pool_trim_count", snapshot.chunkPoolTrimCount, first);
    WriteNumberField(output, "chunk_pool_system_allocation_count", snapshot.chunkPoolSystemAllocationCount, first);
    WriteNumberField(output, "chunk_pool_peak_allocated", snapshot.chunkPoolPeakAllocated, first);
    WriteNumberField(output, "bytes_per_entity", snapshot.bytesPerEntity, first);
    WriteNumberField(output, "allocated_bytes", snapshot.allocatedBytes, first);
    WriteNumberField(output, "side_payload_bytes", snapshot.sidePayloadBytes, first);
    WriteNumberField(output, "committed_bytes", snapshot.committedBytes, first);
    WriteNumberField(output, "free_bytes", snapshot.freeBytes, first);
    WriteNumberField(output, "peak_committed_bytes", snapshot.peakCommittedBytes, first);
    WriteNumberField(output, "chunk_metadata_bytes", snapshot.chunkMetadataBytes, first);
    WriteNumberField(output, "entity_record_bytes", snapshot.entityRecordBytes, first);
    WriteNumberField(output, "tracked_bytes", snapshot.trackedBytes, first);
    WriteNumberField(output, "used_bytes", snapshot.usedBytes, first);
    WriteNumberField(output, "wasted_bytes", snapshot.wastedBytes, first);
    WriteNumberField(output, "hot_table_components", snapshot.hotTableComponents, first);
    WriteNumberField(output, "cold_table_components", snapshot.coldTableComponents, first);
    WriteNumberField(output, "sparse_tag_components", snapshot.sparseTagComponents, first);
    WriteNumberField(output, "sparse_payload_components", snapshot.sparsePayloadComponents, first);
    WriteNumberField(output, "shared_value_components", snapshot.sharedValueComponents, first);
    WriteNumberField(output, "external_blob_components", snapshot.externalBlobComponents, first);
    WriteNumberField(output, "hot_table_used_bytes", snapshot.hotTableUsedBytes, first);
    WriteNumberField(output, "cold_table_used_bytes", snapshot.coldTableUsedBytes, first);
    WriteNumberField(output, "sparse_tag_used_bytes", snapshot.sparseTagUsedBytes, first);
    WriteNumberField(output, "sparse_payload_used_bytes", snapshot.sparsePayloadUsedBytes, first);
    WriteNumberField(output, "shared_value_used_bytes", snapshot.sharedValueUsedBytes, first);
    WriteNumberField(output, "external_blob_used_bytes", snapshot.externalBlobUsedBytes, first);
    WriteNumberField(output, "hot_table_capacity_bytes", snapshot.hotTableCapacityBytes, first);
    WriteNumberField(output, "cold_table_capacity_bytes", snapshot.coldTableCapacityBytes, first);
    WriteNumberField(output, "sparse_tag_capacity_bytes", snapshot.sparseTagCapacityBytes, first);
    WriteNumberField(output, "sparse_payload_capacity_bytes", snapshot.sparsePayloadCapacityBytes, first);
    WriteNumberField(output, "shared_value_capacity_bytes", snapshot.sharedValueCapacityBytes, first);
    WriteNumberField(output, "external_blob_capacity_bytes", snapshot.externalBlobCapacityBytes, first);
    WriteNumberField(output, "storage_system_allocation_count", snapshot.storageSystemAllocationCount, first);
    WriteNumberField(output, "storage_system_allocations_since_reset", snapshot.storageSystemAllocationsSinceReset, first);
    WriteNumberField(output, "occupancy_percent", snapshot.occupancyPercent, first);
    WriteNumberField(output, "fragmentation_percent", snapshot.fragmentationPercent, first);
    WriteNumberField(output, "sparse_chunk_percent", snapshot.sparseChunkPercent, first);
    WriteNumberField(output, "tail_sparse_chunk_percent", snapshot.tailSparseChunkPercent, first);
    WriteNumberField(output, "fragmented_chunk_percent", snapshot.fragmentedChunkPercent, first);
    WriteNumberField(output, "empty_chunk_percent", snapshot.emptyChunkPercent, first);
    WriteNumberField(output, "query_plan_requests", snapshot.queryPlanRequests, first);
    WriteNumberField(output, "query_cache_hits", snapshot.queryCacheHits, first);
    WriteNumberField(output, "query_cache_misses", snapshot.queryCacheMisses, first);
    WriteNumberField(output, "query_plan_builds", snapshot.queryPlanBuilds, first);
    WriteNumberField(output, "query_plan_cache_lookup_elapsed_nanoseconds", snapshot.queryPlanCacheLookupElapsedNanoseconds, first);
    WriteNumberField(output, "query_plan_build_elapsed_nanoseconds", snapshot.queryPlanBuildElapsedNanoseconds, first);
    WriteNumberField(output, "query_record_cache_hits", snapshot.queryRecordCacheHits, first);
    WriteNumberField(output, "query_record_cache_misses", snapshot.queryRecordCacheMisses, first);
    WriteNumberField(output, "query_cache_hit_percent", snapshot.queryCacheHitPercent, first);
    WriteNumberField(output, "query_cache_miss_percent", snapshot.queryCacheMissPercent, first);
    WriteNumberField(output, "query_average_plan_cache_lookup_nanoseconds", snapshot.queryAveragePlanCacheLookupNanoseconds, first);
    WriteNumberField(output, "query_average_plan_build_nanoseconds", snapshot.queryAveragePlanBuildNanoseconds, first);
    WriteNumberField(output, "query_record_cache_hit_percent", snapshot.queryRecordCacheHitPercent, first);
    WriteNumberField(output, "query_record_cache_miss_percent", snapshot.queryRecordCacheMissPercent, first);
    WriteNumberField(output, "query_executions", snapshot.queryExecutions, first);
    WriteNumberField(output, "query_batches", snapshot.queryBatches, first);
    WriteNumberField(output, "query_entities_visited", snapshot.queryEntitiesVisited, first);
    WriteNumberField(output, "query_bytes_touched", snapshot.queryBytesTouched, first);
    WriteNumberField(output, "query_elapsed_nanoseconds", snapshot.queryElapsedNanoseconds, first);
    WriteNumberField(output, "query_prepare_calls", snapshot.queryPrepareCalls, first);
    WriteNumberField(output, "query_prepare_records", snapshot.queryPrepareRecords, first);
    WriteNumberField(output, "query_matched_chunks", snapshot.queryMatchedChunks, first);
    WriteNumberField(output, "query_matched_archetypes", snapshot.queryMatchedArchetypes, first);
    WriteNumberField(output, "query_prepare_elapsed_nanoseconds", snapshot.queryPrepareElapsedNanoseconds, first);
    WriteNumberField(output, "query_kernel_elapsed_nanoseconds", snapshot.queryKernelElapsedNanoseconds, first);
    WriteNumberField(output, "query_adaptive_executions", snapshot.queryAdaptiveExecutions, first);
    WriteNumberField(output, "query_effective_batch_size_total", snapshot.queryEffectiveBatchSizeTotal, first);
    WriteNumberField(output, "query_max_effective_batch_size", snapshot.queryMaxEffectiveBatchSize, first);
    WriteNumberField(output, "query_single_thread_executions", snapshot.querySingleThreadExecutions, first);
    WriteNumberField(output, "query_parallel_chunk_executions", snapshot.queryParallelChunkExecutions, first);
    WriteNumberField(output, "query_parallel_range_executions", snapshot.queryParallelRangeExecutions, first);
    WriteNumberField(output, "query_simd_preferred_executions", snapshot.querySimdPreferredExecutions, first);
    WriteNumberField(output, "query_deterministic_executions", snapshot.queryDeterministicExecutions, first);
    WriteNumberField(output, "query_estimated_bytes_per_second", snapshot.queryEstimatedBytesPerSecond, first);
    WriteNumberField(output, "query_estimated_gigabytes_per_second", snapshot.queryEstimatedGigabytesPerSecond, first);
    WriteNumberField(output, "query_kernel_estimated_bytes_per_second", snapshot.queryKernelEstimatedBytesPerSecond, first);
    WriteNumberField(output, "query_kernel_estimated_gigabytes_per_second", snapshot.queryKernelEstimatedGigabytesPerSecond, first);
    WriteNumberField(output, "query_average_bytes_per_entity", snapshot.queryAverageBytesPerEntity, first);
    WriteNumberField(output, "query_average_prepare_nanoseconds", snapshot.queryAveragePrepareNanoseconds, first);
    WriteNumberField(output, "query_average_matched_chunks", snapshot.queryAverageMatchedChunks, first);
    WriteNumberField(output, "query_average_matched_archetypes", snapshot.queryAverageMatchedArchetypes, first);
    WriteNumberField(output, "query_average_kernel_nanoseconds", snapshot.queryAverageKernelNanoseconds, first);
    WriteNumberField(output, "query_average_effective_batch_size", snapshot.queryAverageEffectiveBatchSize, first);
    WriteNumberField(output, "query_prefetch_distance_total", snapshot.queryPrefetchDistanceTotal, first);
    WriteNumberField(output, "query_average_prefetch_distance", snapshot.queryAveragePrefetchDistance, first);
    WriteNumberField(output, "query_parallel_executions", snapshot.queryParallelExecutions, first);
    WriteNumberField(output, "query_worker_slots", snapshot.queryWorkerSlots, first);
    WriteNumberField(output, "query_worker_active_slots", snapshot.queryWorkerActiveSlots, first);
    WriteNumberField(output, "query_worker_utilization_percent", snapshot.queryWorkerUtilizationPercent, first);
    WriteStringField(output, "preferred_kernel_backend", snapshot.preferredKernelBackend, first);
    WriteNumberField(output, "preferred_kernel_float_lanes", snapshot.preferredKernelFloatLanes, first);
    WriteBoolField(output, "preferred_kernel_backend_compiled", snapshot.preferredKernelBackendCompiled, first);
    WriteBoolField(output, "preferred_kernel_backend_supported", snapshot.preferredKernelBackendSupported, first);
    WriteBoolField(output, "preferred_kernel_backend_auto_selectable", snapshot.preferredKernelBackendAutoSelectable, first);
    WriteBoolField(output, "avx2_kernel_backend_compiled", snapshot.avx2KernelBackendCompiled, first);
    WriteBoolField(output, "avx2_kernel_backend_supported", snapshot.avx2KernelBackendSupported, first);
    WriteBoolField(output, "avx2_kernel_backend_auto_selectable", snapshot.avx2KernelBackendAutoSelectable, first);
    WriteNumberField(output, "compat_mutable_iterations", snapshot.compatMutableIterations, first);
    WriteNumberField(output, "compat_mutable_entities_visited", snapshot.compatMutableEntitiesVisited, first);
    WriteNumberField(output, "structural_changes_since_reset", snapshot.structuralChangesSinceReset, first);
    WriteNumberField(output, "total_structural_changes", snapshot.totalStructuralChanges, first);
    WriteNumberField(output, "archetype_transition_invalidations_since_reset", snapshot.archetypeTransitionInvalidationsSinceReset, first);
    WriteNumberField(output, "total_archetype_transition_invalidations", snapshot.totalArchetypeTransitionInvalidations, first);

    output << "\n}\n";
    return output.str();
}

void ExportWorldTelemetrySnapshotToJsonFile(const WorldTelemetrySnapshot& snapshot, const std::filesystem::path& path) {
    if (path.empty()) {
        throw std::invalid_argument("ECS world telemetry export path is empty");
    }
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        throw std::runtime_error("Failed to open ECS world telemetry export file: " + path.string());
    }
    output << WorldTelemetrySnapshotToJson(snapshot);
    if (!output.good()) {
        throw std::runtime_error("Failed to write ECS world telemetry export file: " + path.string());
    }
}

} // namespace kb::ecs
