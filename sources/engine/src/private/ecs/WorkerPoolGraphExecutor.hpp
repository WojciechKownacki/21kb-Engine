#pragma once
#include "engine/ecs/WorkerPool.hpp"
namespace kb::ecs::detail {
class GraphBridge {
public:
    virtual ~GraphBridge() = default;
    [[nodiscard]] virtual bool IsCurrent(const WorkerPool& pool) const noexcept = 0;
    [[nodiscard]] virtual bool RunJobs(WorkerPool& pool, std::span<const WorkerPoolJob> jobs) = 0;
    [[nodiscard]] virtual bool RunBatches(WorkerPool& pool, std::span<const WorkerPoolBatch> batches,
        WorkerPoolBatchCallback callback, void* context) = 0;
    [[nodiscard]] virtual bool RunChunks(WorkerPool& pool, std::span<const WorkerPoolChunk> chunks,
        WorkerPoolChunkCallback callback, void* context) = 0;
};
[[nodiscard]] WorkerPoolGraphResult ExecuteGraph(WorkerPool& pool, JobGraph graph,
    std::span<const WorkerPoolJob> jobs, std::stop_token cancellation,
    GraphBridgeSlot& bridge, std::mutex& dispatchMutex);
[[nodiscard]] bool IsExecutingGraphOn(const WorkerPool& pool) noexcept;
[[nodiscard]] bool RunNestedJobs(WorkerPool& pool, std::span<const WorkerPoolJob> jobs);
[[nodiscard]] bool RunNestedBatches(WorkerPool& pool, std::span<const WorkerPoolBatch> batches,
    WorkerPoolBatchCallback callback, void* context);
[[nodiscard]] bool RunNestedChunks(WorkerPool& pool, std::span<const WorkerPoolChunk> chunks,
    WorkerPoolChunkCallback callback, void* context);
}
