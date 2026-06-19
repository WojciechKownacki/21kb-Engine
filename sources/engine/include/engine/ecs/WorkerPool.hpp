#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace kb::ecs {

struct WorkerPoolConfig {
    std::size_t workerCount = 0;
    bool pinWorkersToCores = false;
    std::size_t firstPinnedCore = 0;
    bool singleThreaded = false;
    bool collectDispatchTelemetry = false;
};

struct WorkerContext {
    std::size_t workerIndex = 0;
    std::size_t workerCount = 0;
};

using WorkerPoolJobCallback = void (*)(WorkerContext, void*);

struct WorkerPoolJob {
    WorkerPoolJobCallback callback = nullptr;
    void* context = nullptr;
};

inline constexpr std::size_t kAnyWorkerPoolWorker = static_cast<std::size_t>(-1);

struct WorkerPoolBatch {
    std::size_t index = 0;
    std::size_t begin = 0;
    std::size_t count = 0;
    std::size_t preferredWorkerIndex = kAnyWorkerPoolWorker;
    std::size_t workerCountLimit = 0;
};

using WorkerPoolBatchCallback = void (*)(WorkerContext, const WorkerPoolBatch&, void*);

struct WorkerPoolChunk {
    std::size_t index = 0;
    std::size_t begin = 0;
    std::size_t count = 0;
    std::size_t preferredWorkerIndex = kAnyWorkerPoolWorker;
    std::size_t workerCountLimit = 0;
};

using WorkerPoolChunkCallback = void (*)(WorkerContext, const WorkerPoolChunk&, void*);

enum class WorkerPoolDispatchMode {
    None,
    Jobs,
    Batches,
    BatchesStaticStrided,
    Chunks,
    ChunksStaticStrided,
};

struct WorkerPoolDispatchTelemetry {
    WorkerPoolDispatchMode lastMode = WorkerPoolDispatchMode::None;
    std::size_t dispatchCount = 0;
    std::size_t staticStridedDispatchCount = 0;
    std::size_t queuedDispatchCount = 0;
    std::size_t lastWorkItemCount = 0;
    std::size_t lastActiveWorkerCount = 0;
    std::size_t lastConfiguredWorkerCount = 0;
    std::size_t lastQueueOwnerCount = 0;
    std::size_t lastStealCount = 0;
    std::size_t totalStealCount = 0;
    std::uint64_t lastDispatchScheduleNanoseconds = 0;
    std::uint64_t totalDispatchScheduleNanoseconds = 0;
    std::uint64_t averageDispatchScheduleNanoseconds = 0;
    std::uint64_t lastDispatchWallNanoseconds = 0;
    std::uint64_t totalDispatchWallNanoseconds = 0;
    std::uint64_t averageDispatchWallNanoseconds = 0;
    std::uint64_t lastWorkerActiveNanoseconds = 0;
    std::uint64_t totalWorkerActiveNanoseconds = 0;
    std::uint64_t averageWorkerActiveNanoseconds = 0;
    std::uint64_t lastWorkerCapacityNanoseconds = 0;
    std::uint64_t totalWorkerCapacityNanoseconds = 0;
    double lastWorkerUtilizationPercent = 0.0;
    double averageWorkerUtilizationPercent = 0.0;
};

class JobHandle {
public:
    JobHandle() noexcept;
    ~JobHandle();

    JobHandle(const JobHandle&) noexcept = default;
    JobHandle& operator=(const JobHandle&) noexcept = default;
    JobHandle(JobHandle&&) noexcept = default;
    JobHandle& operator=(JobHandle&&) noexcept = default;

    [[nodiscard]] bool Valid() const noexcept;
    [[nodiscard]] bool IsReady() const noexcept;
    void Wait() const;

private:
    class State;

    explicit JobHandle(std::shared_ptr<State> state) noexcept;

    std::shared_ptr<State> state_;

    friend class WorkerPool;
};

class JobFence {
public:
    void Add(JobHandle handle);
    void Clear() noexcept;

    [[nodiscard]] bool Empty() const noexcept;
    [[nodiscard]] std::size_t Count() const noexcept;
    [[nodiscard]] bool IsReady() const noexcept;
    void Wait() const;

private:
    std::vector<JobHandle> handles_;
};

class WorkerPool {
public:
    WorkerPool() = default;
    explicit WorkerPool(WorkerPoolConfig config);
    ~WorkerPool();

    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;
    WorkerPool(WorkerPool&&) = delete;
    WorkerPool& operator=(WorkerPool&&) = delete;

    void Start(WorkerPoolConfig config);
    void Stop();
    void Run(std::span<const WorkerPoolJob> jobs);
    void RunBatches(std::span<const WorkerPoolBatch> batches, WorkerPoolBatchCallback callback, void* context);
    void ParallelForChunks(std::span<const WorkerPoolChunk> chunks, WorkerPoolChunkCallback callback, void* context);
    void ParallelForChunks(std::size_t itemCount, std::size_t chunkSize, WorkerPoolChunkCallback callback, void* context);
    template <typename BatchJob>
    void RunBatches(std::span<const WorkerPoolBatch> batches, BatchJob&& job);
    template <typename ChunkJob>
    void ParallelForChunks(std::span<const WorkerPoolChunk> chunks, ChunkJob&& job);
    template <typename ChunkJob>
    void ParallelForChunks(std::size_t itemCount, std::size_t chunkSize, ChunkJob&& job);
    [[nodiscard]] JobHandle Submit(std::vector<WorkerPoolJob> jobs);
    [[nodiscard]] JobHandle SubmitBatches(std::vector<WorkerPoolBatch> batches, WorkerPoolBatchCallback callback, void* context);
    [[nodiscard]] JobHandle SubmitParallelForChunks(std::vector<WorkerPoolChunk> chunks, WorkerPoolChunkCallback callback, void* context);
    [[nodiscard]] JobHandle SubmitParallelForChunks(std::size_t itemCount, std::size_t chunkSize, WorkerPoolChunkCallback callback, void* context);

    [[nodiscard]] bool Running() const noexcept;
    [[nodiscard]] std::size_t WorkerCount() const noexcept;
    [[nodiscard]] WorkerPoolConfig Config() const noexcept;
    [[nodiscard]] WorkerPoolDispatchTelemetry DispatchTelemetry() const noexcept;

    [[nodiscard]] static std::size_t DefaultWorkerCount() noexcept;
    [[nodiscard]] static std::size_t ResolveWorkerCount(std::size_t requestedWorkerCount) noexcept;
    [[nodiscard]] static bool AffinitySupported() noexcept;

private:
    class WorkerPoolState;

    std::unique_ptr<WorkerPoolState> state_;
};

template <typename BatchJob>
void WorkerPool::RunBatches(std::span<const WorkerPoolBatch> batches, BatchJob&& job) {
    using JobType = std::remove_reference_t<BatchJob>;
    JobType* jobPtr = std::addressof(job);
    RunBatches(
        batches,
        [](WorkerContext context, const WorkerPoolBatch& batch, void* callbackContext) {
            (*static_cast<JobType*>(callbackContext))(context, batch);
        },
        jobPtr);
}

template <typename ChunkJob>
void WorkerPool::ParallelForChunks(std::span<const WorkerPoolChunk> chunks, ChunkJob&& job) {
    using JobType = std::remove_reference_t<ChunkJob>;
    JobType* jobPtr = std::addressof(job);
    ParallelForChunks(
        chunks,
        [](WorkerContext context, const WorkerPoolChunk& chunk, void* callbackContext) {
            (*static_cast<JobType*>(callbackContext))(context, chunk);
        },
        jobPtr);
}

template <typename ChunkJob>
void WorkerPool::ParallelForChunks(std::size_t itemCount, std::size_t chunkSize, ChunkJob&& job) {
    using JobType = std::remove_reference_t<ChunkJob>;
    JobType* jobPtr = std::addressof(job);
    ParallelForChunks(
        itemCount,
        chunkSize,
        [](WorkerContext context, const WorkerPoolChunk& chunk, void* callbackContext) {
            (*static_cast<JobType*>(callbackContext))(context, chunk);
        },
        jobPtr);
}

} // namespace kb::ecs
