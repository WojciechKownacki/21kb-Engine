#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace kb::ecs {

struct WorkerPoolConfig {
    std::size_t workerCount = 0;
    bool pinWorkersToCores = false;
    std::size_t firstPinnedCore = 0;
    bool singleThreaded = false;
};

struct WorkerContext {
    std::size_t workerIndex = 0;
    std::size_t workerCount = 0;
};

using WorkerPoolJob = std::function<void(WorkerContext)>;

inline constexpr std::size_t kAnyWorkerPoolWorker = static_cast<std::size_t>(-1);

struct WorkerPoolBatch {
    std::size_t index = 0;
    std::size_t begin = 0;
    std::size_t count = 0;
    std::size_t preferredWorkerIndex = kAnyWorkerPoolWorker;
};

using WorkerPoolBatchJob = std::function<void(WorkerContext, const WorkerPoolBatch&)>;

struct WorkerPoolChunk {
    std::size_t index = 0;
    std::size_t begin = 0;
    std::size_t count = 0;
    std::size_t preferredWorkerIndex = kAnyWorkerPoolWorker;
};

using WorkerPoolChunkJob = std::function<void(WorkerContext, const WorkerPoolChunk&)>;

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
    void RunBatches(std::span<const WorkerPoolBatch> batches, const WorkerPoolBatchJob& job);
    void ParallelForChunks(std::span<const WorkerPoolChunk> chunks, const WorkerPoolChunkJob& job);
    void ParallelForChunks(std::size_t itemCount, std::size_t chunkSize, const WorkerPoolChunkJob& job);
    [[nodiscard]] JobHandle Submit(std::vector<WorkerPoolJob> jobs);
    [[nodiscard]] JobHandle SubmitBatches(std::vector<WorkerPoolBatch> batches, WorkerPoolBatchJob job);
    [[nodiscard]] JobHandle SubmitParallelForChunks(std::vector<WorkerPoolChunk> chunks, WorkerPoolChunkJob job);
    [[nodiscard]] JobHandle SubmitParallelForChunks(std::size_t itemCount, std::size_t chunkSize, WorkerPoolChunkJob job);

    [[nodiscard]] bool Running() const noexcept;
    [[nodiscard]] std::size_t WorkerCount() const noexcept;
    [[nodiscard]] WorkerPoolConfig Config() const noexcept;

    [[nodiscard]] static std::size_t DefaultWorkerCount() noexcept;
    [[nodiscard]] static std::size_t ResolveWorkerCount(std::size_t requestedWorkerCount) noexcept;
    [[nodiscard]] static bool AffinitySupported() noexcept;

private:
    class WorkerPoolState;

    std::unique_ptr<WorkerPoolState> state_;
};

} // namespace kb::ecs
