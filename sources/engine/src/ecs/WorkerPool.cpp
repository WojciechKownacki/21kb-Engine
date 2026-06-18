#include "engine/ecs/WorkerPool.hpp"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <exception>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <cerrno>
#include <cstring>
#include <pthread.h>
#include <sched.h>
#endif

namespace kb::ecs {
namespace {

[[nodiscard]] std::size_t HardwareThreadCount() noexcept {
    return static_cast<std::size_t>(std::max(1U, std::thread::hardware_concurrency()));
}

void PinCurrentThreadToCore(std::size_t coreIndex) {
#if defined(_WIN32)
    if (coreIndex >= std::numeric_limits<DWORD_PTR>::digits) {
        throw std::invalid_argument("ECS worker affinity core index is outside the supported processor mask");
    }

    DWORD_PTR processMask = 0;
    DWORD_PTR systemMask = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &processMask, &systemMask) == 0) {
        throw std::runtime_error("ECS worker affinity could not read process affinity mask");
    }

    const DWORD_PTR threadMask = static_cast<DWORD_PTR>(1) << coreIndex;
    if ((processMask & threadMask) == 0) {
        throw std::invalid_argument("ECS worker affinity core is not available to the current process");
    }
    if (SetThreadAffinityMask(GetCurrentThread(), threadMask) == 0) {
        throw std::runtime_error("ECS worker affinity could not set thread affinity mask");
    }
#elif defined(__linux__)
    if (coreIndex >= CPU_SETSIZE) {
        throw std::invalid_argument("ECS worker affinity core index is outside the supported CPU set");
    }

    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(coreIndex, &set);
    const int result = pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
    if (result != 0) {
        throw std::runtime_error("ECS worker affinity failed: " + std::string{ std::strerror(result) });
    }
#else
    static_cast<void>(coreIndex);
    throw std::runtime_error("ECS worker affinity is not supported on this platform");
#endif
}

} // namespace

class JobHandle::State {
public:
    void Complete(std::exception_ptr exception) {
        {
            std::lock_guard lock{ mutex_ };
            if (completed_) {
                return;
            }
            exception_ = exception;
            completed_ = true;
        }
        completedCondition_.notify_all();
    }

    [[nodiscard]] bool IsReady() const noexcept {
        std::lock_guard lock{ mutex_ };
        return completed_;
    }

    void Wait() const {
        std::unique_lock lock{ mutex_ };
        completedCondition_.wait(lock, [this] {
            return completed_;
        });
        if (exception_ != nullptr) {
            std::rethrow_exception(exception_);
        }
    }

private:
    mutable std::mutex mutex_;
    mutable std::condition_variable completedCondition_;
    std::exception_ptr exception_;
    bool completed_ = false;
};

JobHandle::JobHandle() noexcept = default;

JobHandle::~JobHandle() = default;

JobHandle::JobHandle(std::shared_ptr<State> state) noexcept
    : state_(std::move(state)) {}

bool JobHandle::Valid() const noexcept {
    return state_ != nullptr;
}

bool JobHandle::IsReady() const noexcept {
    return state_ == nullptr || state_->IsReady();
}

void JobHandle::Wait() const {
    if (state_ != nullptr) {
        state_->Wait();
    }
}

void JobFence::Add(JobHandle handle) {
    if (handle.Valid()) {
        handles_.push_back(std::move(handle));
    }
}

void JobFence::Clear() noexcept {
    handles_.clear();
}

bool JobFence::Empty() const noexcept {
    return handles_.empty();
}

std::size_t JobFence::Count() const noexcept {
    return handles_.size();
}

bool JobFence::IsReady() const noexcept {
    return std::all_of(handles_.begin(), handles_.end(), [](const JobHandle& handle) {
        return handle.IsReady();
    });
}

void JobFence::Wait() const {
    std::exception_ptr firstException;
    for (const JobHandle& handle : handles_) {
        try {
            handle.Wait();
        } catch (...) {
            if (firstException == nullptr) {
                firstException = std::current_exception();
            }
        }
    }
    if (firstException != nullptr) {
        std::rethrow_exception(firstException);
    }
}

class WorkerPool::WorkerPoolState {
public:
    explicit WorkerPoolState(WorkerPoolConfig config)
        : config_(config) {
        config_.workerCount = config_.singleThreaded ? 1U : WorkerPool::ResolveWorkerCount(config_.workerCount);
    }

    ~WorkerPoolState() {
        Stop();
    }

    void Start() {
        std::unique_lock lock{ mutex_ };
        if (running_) {
            return;
        }

        if (config_.singleThreaded) {
            running_ = true;
            stopping_ = false;
            return;
        }

        if (config_.pinWorkersToCores && !WorkerPool::AffinitySupported()) {
            throw std::runtime_error("ECS worker affinity is not supported on this platform");
        }
        if (config_.pinWorkersToCores && HardwareThreadCount() < config_.firstPinnedCore + config_.workerCount) {
            throw std::invalid_argument("ECS worker affinity requires one available logical processor per worker");
        }

        running_ = true;
        stopping_ = false;
        startupRemaining_ = config_.workerCount;
        startupException_ = nullptr;
        workers_.reserve(config_.workerCount);

        try {
            for (std::size_t workerIndex = 0; workerIndex < config_.workerCount; ++workerIndex) {
                workers_.emplace_back([this, workerIndex] {
                    WorkerLoop(workerIndex);
                });
            }
        } catch (...) {
            stopping_ = true;
            batchAvailable_.notify_all();
            lock.unlock();
            JoinWorkers();
            lock.lock();
            workers_.clear();
            running_ = false;
            stopping_ = false;
            startupRemaining_ = 0;
            throw;
        }

        startupComplete_.wait(lock, [this] {
            return startupRemaining_ == 0;
        });

        if (startupException_ != nullptr) {
            stopping_ = true;
            batchAvailable_.notify_all();
            lock.unlock();
            JoinWorkers();
            lock.lock();
            workers_.clear();
            running_ = false;
            stopping_ = false;
            startupRemaining_ = 0;
            std::exception_ptr exception = startupException_;
            startupException_ = nullptr;
            std::rethrow_exception(exception);
        }
    }

    void Stop() {
        std::shared_ptr<JobHandle::State> cancelledCompletion;
        std::exception_ptr cancelledCompletionException;
        {
            std::lock_guard lock{ mutex_ };
            if (!running_) {
                return;
            }
            stopping_ = true;
            batchAvailable_.notify_all();
        }

        JoinWorkers();

        {
            std::lock_guard lock{ mutex_ };
            if (batchActive_) {
                const std::exception_ptr cancellation = firstJobException_ != nullptr
                    ? firstJobException_
                    : std::make_exception_ptr(std::runtime_error("ECS worker pool stopped before submitted work completed"));
                if (activeCompletion_ != nullptr) {
                    cancelledCompletion = activeCompletion_;
                    cancelledCompletionException = cancellation;
                } else {
                    cancelledRunException_ = cancellation;
                }
            }
            workers_.clear();
            jobs_ = nullptr;
            jobCount_ = 0;
            nextJob_ = 0;
            batches_ = nullptr;
            chunks_ = nullptr;
            batchCallback_ = nullptr;
            batchCallbackContext_ = nullptr;
            chunkCallback_ = nullptr;
            chunkCallbackContext_ = nullptr;
            ownedJobs_.clear();
            ownedBatches_.clear();
            ownedChunks_.clear();
            workerBatchQueues_.clear();
            remainingWork_ = 0;
            workMode_ = WorkMode::None;
            batchActive_ = false;
            activeCompletion_.reset();
            running_ = false;
            stopping_ = false;
            startupRemaining_ = 0;
            startupException_ = nullptr;
            firstJobException_ = nullptr;
        }
        if (cancelledCompletion != nullptr) {
            cancelledCompletion->Complete(cancelledCompletionException);
        }
        batchFinished_.notify_all();
    }

    void Run(std::span<const WorkerPoolJob> jobs) {
        if (jobs.empty()) {
            return;
        }
        ValidateJobs(jobs);

        if (config_.singleThreaded) {
            if (!Running()) {
                throw std::logic_error("ECS worker pool must be started before running jobs");
            }
            RunJobsInline(jobs);
            return;
        }

        {
            std::unique_lock lock{ mutex_ };
            if (!running_) {
                throw std::logic_error("ECS worker pool must be started before running jobs");
            }
            batchFinished_.wait(lock, [this] {
                return !batchActive_;
            });
            jobs_ = jobs.data();
            jobCount_ = jobs.size();
            nextJob_ = 0;
            remainingWork_ = jobs.size();
            firstJobException_ = nullptr;
            cancelledRunException_ = nullptr;
            activeWorkerLimit_ = config_.workerCount;
            workMode_ = WorkMode::Jobs;
            batchActive_ = true;
        }

        batchAvailable_.notify_all();

        std::unique_lock lock{ mutex_ };
        batchFinished_.wait(lock, [this] {
            return !batchActive_;
        });

        if (firstJobException_ != nullptr) {
            std::exception_ptr exception = firstJobException_;
            firstJobException_ = nullptr;
            std::rethrow_exception(exception);
        }
        if (cancelledRunException_ != nullptr) {
            std::exception_ptr exception = cancelledRunException_;
            cancelledRunException_ = nullptr;
            std::rethrow_exception(exception);
        }
    }

    void RunBatches(std::span<const WorkerPoolBatch> batches, WorkerPoolBatchCallback callback, void* context) {
        if (batches.empty()) {
            return;
        }
        if (callback == nullptr) {
            throw std::invalid_argument("ECS worker pool batch job must be callable");
        }

        if (config_.singleThreaded) {
            if (!Running()) {
                throw std::logic_error("ECS worker pool must be started before running batches");
            }
            RunBatchesInline(batches, callback, context);
            return;
        }

        {
            std::unique_lock lock{ mutex_ };
            if (!running_) {
                throw std::logic_error("ECS worker pool must be started before running batches");
            }
            batchFinished_.wait(lock, [this] {
                return !batchActive_;
            });

            batches_ = batches.data();
            batchCallback_ = callback;
            batchCallbackContext_ = context;
            activeWorkerLimit_ = ResolveBatchWorkerLimit(batches);
            workerBatchQueues_.assign(config_.workerCount, {});
            for (std::size_t batchIndex = 0; batchIndex < batches.size(); ++batchIndex) {
                const WorkerPoolBatch& batch = batches[batchIndex];
                const std::size_t owner = batch.preferredWorkerIndex == kAnyWorkerPoolWorker ? batchIndex % activeWorkerLimit_ : batch.preferredWorkerIndex % activeWorkerLimit_;
                workerBatchQueues_[owner].push_back(batchIndex);
            }

            remainingWork_ = batches.size();
            firstJobException_ = nullptr;
            cancelledRunException_ = nullptr;
            workMode_ = WorkMode::Batches;
            batchActive_ = true;
        }

        batchAvailable_.notify_all();

        std::unique_lock lock{ mutex_ };
        batchFinished_.wait(lock, [this] {
            return !batchActive_;
        });

        if (firstJobException_ != nullptr) {
            std::exception_ptr exception = firstJobException_;
            firstJobException_ = nullptr;
            std::rethrow_exception(exception);
        }
        if (cancelledRunException_ != nullptr) {
            std::exception_ptr exception = cancelledRunException_;
            cancelledRunException_ = nullptr;
            std::rethrow_exception(exception);
        }
    }

    void ParallelForChunks(std::span<const WorkerPoolChunk> chunks, WorkerPoolChunkCallback callback, void* context) {
        if (chunks.empty()) {
            return;
        }
        if (callback == nullptr) {
            throw std::invalid_argument("ECS worker pool chunk job must be callable");
        }

        if (config_.singleThreaded) {
            if (!Running()) {
                throw std::logic_error("ECS worker pool must be started before running chunks");
            }
            RunChunksInline(chunks, callback, context);
            return;
        }

        {
            std::unique_lock lock{ mutex_ };
            if (!running_) {
                throw std::logic_error("ECS worker pool must be started before running chunks");
            }
            batchFinished_.wait(lock, [this] {
                return !batchActive_;
            });

            chunks_ = chunks.data();
            chunkCallback_ = callback;
            chunkCallbackContext_ = context;
            activeWorkerLimit_ = ResolveChunkWorkerLimit(chunks);
            workerBatchQueues_.assign(config_.workerCount, {});
            for (std::size_t chunkIndex = 0; chunkIndex < chunks.size(); ++chunkIndex) {
                const WorkerPoolChunk& chunk = chunks[chunkIndex];
                const std::size_t owner = chunk.preferredWorkerIndex == kAnyWorkerPoolWorker ? chunkIndex % activeWorkerLimit_ : chunk.preferredWorkerIndex % activeWorkerLimit_;
                workerBatchQueues_[owner].push_back(chunkIndex);
            }

            remainingWork_ = chunks.size();
            firstJobException_ = nullptr;
            cancelledRunException_ = nullptr;
            workMode_ = WorkMode::Chunks;
            batchActive_ = true;
        }

        batchAvailable_.notify_all();

        std::unique_lock lock{ mutex_ };
        batchFinished_.wait(lock, [this] {
            return !batchActive_;
        });

        if (firstJobException_ != nullptr) {
            std::exception_ptr exception = firstJobException_;
            firstJobException_ = nullptr;
            std::rethrow_exception(exception);
        }
        if (cancelledRunException_ != nullptr) {
            std::exception_ptr exception = cancelledRunException_;
            cancelledRunException_ = nullptr;
            std::rethrow_exception(exception);
        }
    }

    [[nodiscard]] JobHandle Submit(std::vector<WorkerPoolJob> jobs) {
        if (jobs.empty()) {
            return {};
        }
        ValidateJobs(jobs);

        if (config_.singleThreaded) {
            if (!Running()) {
                throw std::logic_error("ECS worker pool must be started before submitting jobs");
            }
            return SubmitJobsInline(jobs);
        }

        auto completion = std::make_shared<JobHandle::State>();
        {
            std::unique_lock lock{ mutex_ };
            if (!running_) {
                throw std::logic_error("ECS worker pool must be started before submitting jobs");
            }
            batchFinished_.wait(lock, [this] {
                return !batchActive_;
            });

            ownedJobs_ = std::move(jobs);
            jobs_ = ownedJobs_.data();
            jobCount_ = ownedJobs_.size();
            nextJob_ = 0;
            remainingWork_ = ownedJobs_.size();
            firstJobException_ = nullptr;
            cancelledRunException_ = nullptr;
            activeWorkerLimit_ = config_.workerCount;
            activeCompletion_ = completion;
            workMode_ = WorkMode::Jobs;
            batchActive_ = true;
        }

        batchAvailable_.notify_all();
        return JobHandle{ std::move(completion) };
    }

    [[nodiscard]] JobHandle SubmitBatches(std::vector<WorkerPoolBatch> batches, WorkerPoolBatchCallback callback, void* context) {
        if (batches.empty()) {
            return {};
        }
        if (callback == nullptr) {
            throw std::invalid_argument("ECS worker pool batch job must be callable");
        }

        if (config_.singleThreaded) {
            if (!Running()) {
                throw std::logic_error("ECS worker pool must be started before submitting batches");
            }
            return SubmitBatchesInline(batches, callback, context);
        }

        auto completion = std::make_shared<JobHandle::State>();
        {
            std::unique_lock lock{ mutex_ };
            if (!running_) {
                throw std::logic_error("ECS worker pool must be started before submitting batches");
            }
            batchFinished_.wait(lock, [this] {
                return !batchActive_;
            });

            ownedBatches_ = std::move(batches);
            batches_ = ownedBatches_.data();
            batchCallback_ = callback;
            batchCallbackContext_ = context;
            activeWorkerLimit_ = ResolveBatchWorkerLimit(ownedBatches_);
            workerBatchQueues_.assign(config_.workerCount, {});
            for (std::size_t batchIndex = 0; batchIndex < ownedBatches_.size(); ++batchIndex) {
                const WorkerPoolBatch& batch = ownedBatches_[batchIndex];
                const std::size_t owner = batch.preferredWorkerIndex == kAnyWorkerPoolWorker ? batchIndex % activeWorkerLimit_ : batch.preferredWorkerIndex % activeWorkerLimit_;
                workerBatchQueues_[owner].push_back(batchIndex);
            }

            remainingWork_ = ownedBatches_.size();
            firstJobException_ = nullptr;
            cancelledRunException_ = nullptr;
            activeCompletion_ = completion;
            workMode_ = WorkMode::Batches;
            batchActive_ = true;
        }

        batchAvailable_.notify_all();
        return JobHandle{ std::move(completion) };
    }

    [[nodiscard]] JobHandle SubmitChunks(std::vector<WorkerPoolChunk> chunks, WorkerPoolChunkCallback callback, void* context) {
        if (chunks.empty()) {
            return {};
        }
        if (callback == nullptr) {
            throw std::invalid_argument("ECS worker pool chunk job must be callable");
        }

        if (config_.singleThreaded) {
            if (!Running()) {
                throw std::logic_error("ECS worker pool must be started before submitting chunks");
            }
            return SubmitChunksInline(chunks, callback, context);
        }

        auto completion = std::make_shared<JobHandle::State>();
        {
            std::unique_lock lock{ mutex_ };
            if (!running_) {
                throw std::logic_error("ECS worker pool must be started before submitting chunks");
            }
            batchFinished_.wait(lock, [this] {
                return !batchActive_;
            });

            ownedChunks_ = std::move(chunks);
            chunks_ = ownedChunks_.data();
            chunkCallback_ = callback;
            chunkCallbackContext_ = context;
            activeWorkerLimit_ = ResolveChunkWorkerLimit(ownedChunks_);
            workerBatchQueues_.assign(config_.workerCount, {});
            for (std::size_t chunkIndex = 0; chunkIndex < ownedChunks_.size(); ++chunkIndex) {
                const WorkerPoolChunk& chunk = ownedChunks_[chunkIndex];
                const std::size_t owner = chunk.preferredWorkerIndex == kAnyWorkerPoolWorker ? chunkIndex % activeWorkerLimit_ : chunk.preferredWorkerIndex % activeWorkerLimit_;
                workerBatchQueues_[owner].push_back(chunkIndex);
            }

            remainingWork_ = ownedChunks_.size();
            firstJobException_ = nullptr;
            cancelledRunException_ = nullptr;
            activeCompletion_ = completion;
            workMode_ = WorkMode::Chunks;
            batchActive_ = true;
        }

        batchAvailable_.notify_all();
        return JobHandle{ std::move(completion) };
    }

    [[nodiscard]] bool Running() const noexcept {
        std::lock_guard lock{ mutex_ };
        return running_;
    }

    [[nodiscard]] WorkerPoolConfig Config() const noexcept {
        return config_;
    }

private:
    enum class WorkMode {
        None,
        Jobs,
        Batches,
        Chunks,
    };

    static void ThrowIfFailed(std::exception_ptr exception) {
        if (exception != nullptr) {
            std::rethrow_exception(exception);
        }
    }

    static void ValidateJobs(std::span<const WorkerPoolJob> jobs) {
        for (const WorkerPoolJob& job : jobs) {
            if (job.callback == nullptr) {
                throw std::invalid_argument("ECS worker pool job must be callable");
            }
        }
    }

    std::size_t ResolveWorkerLimit(std::size_t requestedWorkerLimit) const noexcept {
        if (requestedWorkerLimit == 0U) {
            return config_.workerCount;
        }
        return std::clamp<std::size_t>(requestedWorkerLimit, 1U, config_.workerCount);
    }

    std::size_t ResolveBatchWorkerLimit(std::span<const WorkerPoolBatch> batches) const noexcept {
        std::size_t workerLimit = 0;
        for (const WorkerPoolBatch& batch : batches) {
            if (batch.workerCountLimit != 0U) {
                workerLimit = workerLimit == 0U ? batch.workerCountLimit : std::min(workerLimit, batch.workerCountLimit);
            }
        }
        return ResolveWorkerLimit(workerLimit);
    }

    std::size_t ResolveChunkWorkerLimit(std::span<const WorkerPoolChunk> chunks) const noexcept {
        std::size_t workerLimit = 0;
        for (const WorkerPoolChunk& chunk : chunks) {
            if (chunk.workerCountLimit != 0U) {
                workerLimit = workerLimit == 0U ? chunk.workerCountLimit : std::min(workerLimit, chunk.workerCountLimit);
            }
        }
        return ResolveWorkerLimit(workerLimit);
    }

    static void RunJobsInline(std::span<const WorkerPoolJob> jobs) {
        std::exception_ptr firstException;
        const WorkerContext context{
            .workerIndex = 0,
            .workerCount = 1,
        };

        for (const WorkerPoolJob& job : jobs) {
            try {
                job.callback(context, job.context);
            } catch (...) {
                if (firstException == nullptr) {
                    firstException = std::current_exception();
                }
                break;
            }
        }

        ThrowIfFailed(firstException);
    }

    static void RunBatchesInline(std::span<const WorkerPoolBatch> batches, WorkerPoolBatchCallback callback, void* callbackContext) {
        std::exception_ptr firstException;
        const WorkerContext context{
            .workerIndex = 0,
            .workerCount = 1,
        };

        for (const WorkerPoolBatch& batch : batches) {
            try {
                callback(context, batch, callbackContext);
            } catch (...) {
                if (firstException == nullptr) {
                    firstException = std::current_exception();
                }
                break;
            }
        }

        ThrowIfFailed(firstException);
    }

    static void RunChunksInline(std::span<const WorkerPoolChunk> chunks, WorkerPoolChunkCallback callback, void* callbackContext) {
        std::exception_ptr firstException;
        const WorkerContext context{
            .workerIndex = 0,
            .workerCount = 1,
        };

        for (const WorkerPoolChunk& chunk : chunks) {
            try {
                callback(context, chunk, callbackContext);
            } catch (...) {
                if (firstException == nullptr) {
                    firstException = std::current_exception();
                }
                break;
            }
        }

        ThrowIfFailed(firstException);
    }

    static JobHandle SubmitJobsInline(std::span<const WorkerPoolJob> jobs) {
        auto completion = std::make_shared<JobHandle::State>();
        std::exception_ptr firstException;
        const WorkerContext context{
            .workerIndex = 0,
            .workerCount = 1,
        };

        for (const WorkerPoolJob& job : jobs) {
            try {
                job.callback(context, job.context);
            } catch (...) {
                if (firstException == nullptr) {
                    firstException = std::current_exception();
                }
                break;
            }
        }

        completion->Complete(firstException);
        return JobHandle{ std::move(completion) };
    }

    static JobHandle SubmitBatchesInline(std::span<const WorkerPoolBatch> batches, WorkerPoolBatchCallback callback, void* callbackContext) {
        auto completion = std::make_shared<JobHandle::State>();
        std::exception_ptr firstException;
        const WorkerContext context{
            .workerIndex = 0,
            .workerCount = 1,
        };

        for (const WorkerPoolBatch& batch : batches) {
            try {
                callback(context, batch, callbackContext);
            } catch (...) {
                if (firstException == nullptr) {
                    firstException = std::current_exception();
                }
                break;
            }
        }

        completion->Complete(firstException);
        return JobHandle{ std::move(completion) };
    }

    static JobHandle SubmitChunksInline(std::span<const WorkerPoolChunk> chunks, WorkerPoolChunkCallback callback, void* callbackContext) {
        auto completion = std::make_shared<JobHandle::State>();
        std::exception_ptr firstException;
        const WorkerContext context{
            .workerIndex = 0,
            .workerCount = 1,
        };

        for (const WorkerPoolChunk& chunk : chunks) {
            try {
                callback(context, chunk, callbackContext);
            } catch (...) {
                if (firstException == nullptr) {
                    firstException = std::current_exception();
                }
                break;
            }
        }

        completion->Complete(firstException);
        return JobHandle{ std::move(completion) };
    }

    void WorkerLoop(std::size_t workerIndex) {
        try {
            if (config_.pinWorkersToCores) {
                PinCurrentThreadToCore(config_.firstPinnedCore + workerIndex);
            }
        } catch (...) {
            std::lock_guard lock{ mutex_ };
            if (startupException_ == nullptr) {
                startupException_ = std::current_exception();
            }
        }

        {
            std::lock_guard lock{ mutex_ };
            --startupRemaining_;
            startupComplete_.notify_one();
        }

        while (true) {
            const WorkerPoolJob* job = nullptr;
            WorkerPoolBatchCallback batchCallback = nullptr;
            void* batchCallbackContext = nullptr;
            WorkerPoolChunkCallback chunkCallback = nullptr;
            void* chunkCallbackContext = nullptr;
            WorkerPoolBatch batch;
            WorkerPoolChunk chunk;
            WorkMode mode = WorkMode::None;
            std::size_t workerCount = config_.workerCount;
            {
                std::unique_lock lock{ mutex_ };
                batchAvailable_.wait(lock, [this, workerIndex] {
                    return stopping_ || HasPendingWorkLocked(workerIndex);
                });

                if (stopping_) {
                    return;
                }

                mode = workMode_;
                workerCount = activeWorkerLimit_;
                if (mode == WorkMode::Jobs) {
                    job = &jobs_[nextJob_];
                    ++nextJob_;
                } else if (mode == WorkMode::Batches) {
                    const std::size_t batchIndex = TakeBatchLocked(workerIndex);
                    batch = batches_[batchIndex];
                    batchCallback = batchCallback_;
                    batchCallbackContext = batchCallbackContext_;
                } else if (mode == WorkMode::Chunks) {
                    const std::size_t chunkIndex = TakeBatchLocked(workerIndex);
                    chunk = chunks_[chunkIndex];
                    chunkCallback = chunkCallback_;
                    chunkCallbackContext = chunkCallbackContext_;
                }
            }

            const WorkerContext context{
                .workerIndex = workerIndex,
                .workerCount = workerCount,
            };

            try {
                if (mode == WorkMode::Jobs) {
                    job->callback(context, job->context);
                } else if (mode == WorkMode::Batches) {
                    batchCallback(context, batch, batchCallbackContext);
                } else if (mode == WorkMode::Chunks) {
                    chunkCallback(context, chunk, chunkCallbackContext);
                }
            } catch (...) {
                std::lock_guard lock{ mutex_ };
                if (firstJobException_ == nullptr) {
                    firstJobException_ = std::current_exception();
                    CancelPendingWorkLocked();
                }
            }

            {
                std::lock_guard lock{ mutex_ };
                --remainingWork_;
                if (remainingWork_ == 0) {
                    CompleteActiveWorkLocked();
                    ClearActiveWorkLocked();
                    batchFinished_.notify_all();
                } else if (HasAnyPendingWorkLocked()) {
                    batchAvailable_.notify_one();
                }
            }
        }
    }

    [[nodiscard]] bool HasPendingWorkLocked(std::size_t workerIndex) const {
        if (!batchActive_) {
            return false;
        }
        if (workerIndex >= activeWorkerLimit_) {
            return false;
        }
        if (workMode_ == WorkMode::Jobs) {
            return nextJob_ < jobCount_;
        }
        if (workMode_ != WorkMode::Batches && workMode_ != WorkMode::Chunks) {
            return false;
        }
        if (workerIndex < workerBatchQueues_.size() && !workerBatchQueues_[workerIndex].empty()) {
            return true;
        }
        for (std::size_t index = 0; index < activeWorkerLimit_; ++index) {
            if (index != workerIndex && !workerBatchQueues_[index].empty()) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool HasAnyPendingWorkLocked() const {
        if (!batchActive_) {
            return false;
        }
        if (workMode_ == WorkMode::Jobs) {
            return nextJob_ < jobCount_;
        }
        if (workMode_ != WorkMode::Batches && workMode_ != WorkMode::Chunks) {
            return false;
        }
        for (const auto& queue : workerBatchQueues_) {
            if (!queue.empty()) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::size_t TakeBatchLocked(std::size_t workerIndex) {
        auto& localQueue = workerBatchQueues_[workerIndex];
        if (!localQueue.empty()) {
            const std::size_t batchIndex = localQueue.front();
            localQueue.pop_front();
            return batchIndex;
        }

        for (std::size_t offset = 1; offset <= activeWorkerLimit_; ++offset) {
            const std::size_t victimIndex = (workerIndex + offset) % activeWorkerLimit_;
            auto& victimQueue = workerBatchQueues_[victimIndex];
            if (!victimQueue.empty()) {
                const std::size_t batchIndex = victimQueue.back();
                victimQueue.pop_back();
                return batchIndex;
            }
        }

        throw std::logic_error("ECS worker pool attempted to take a missing batch");
    }

    void CancelPendingWorkLocked() noexcept {
        std::size_t pendingWork = 0;
        if (workMode_ == WorkMode::Jobs) {
            pendingWork = jobCount_ > nextJob_ ? jobCount_ - nextJob_ : 0U;
            nextJob_ = jobCount_;
        } else if (workMode_ == WorkMode::Batches || workMode_ == WorkMode::Chunks) {
            for (auto& queue : workerBatchQueues_) {
                pendingWork += queue.size();
                queue.clear();
            }
        }

        remainingWork_ = pendingWork > remainingWork_ ? 0U : remainingWork_ - pendingWork;
        batchAvailable_.notify_all();
    }

    void ClearActiveWorkLocked() noexcept {
        jobs_ = nullptr;
        jobCount_ = 0;
        nextJob_ = 0;
        batches_ = nullptr;
        chunks_ = nullptr;
        batchCallback_ = nullptr;
        batchCallbackContext_ = nullptr;
        chunkCallback_ = nullptr;
        chunkCallbackContext_ = nullptr;
        activeWorkerLimit_ = config_.workerCount;
        ownedJobs_.clear();
        ownedBatches_.clear();
        ownedChunks_.clear();
        workerBatchQueues_.clear();
        remainingWork_ = 0;
        workMode_ = WorkMode::None;
        batchActive_ = false;
        activeCompletion_.reset();
    }

    void CompleteActiveWorkLocked() {
        if (activeCompletion_ != nullptr) {
            activeCompletion_->Complete(firstJobException_);
        }
    }

    void JoinWorkers() {
        for (std::thread& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    WorkerPoolConfig config_;
    mutable std::mutex mutex_;
    std::condition_variable batchAvailable_;
    std::condition_variable batchFinished_;
    std::condition_variable startupComplete_;
    std::vector<std::thread> workers_;
    const WorkerPoolJob* jobs_ = nullptr;
    std::size_t jobCount_ = 0;
    std::size_t nextJob_ = 0;
    const WorkerPoolBatch* batches_ = nullptr;
    const WorkerPoolChunk* chunks_ = nullptr;
    WorkerPoolBatchCallback batchCallback_ = nullptr;
    void* batchCallbackContext_ = nullptr;
    WorkerPoolChunkCallback chunkCallback_ = nullptr;
    void* chunkCallbackContext_ = nullptr;
    std::vector<WorkerPoolJob> ownedJobs_;
    std::vector<WorkerPoolBatch> ownedBatches_;
    std::vector<WorkerPoolChunk> ownedChunks_;
    std::vector<std::deque<std::size_t>> workerBatchQueues_;
    std::size_t activeWorkerLimit_ = 1;
    std::size_t remainingWork_ = 0;
    std::size_t startupRemaining_ = 0;
    std::exception_ptr startupException_;
    std::exception_ptr firstJobException_;
    std::exception_ptr cancelledRunException_;
    std::shared_ptr<JobHandle::State> activeCompletion_;
    WorkMode workMode_ = WorkMode::None;
    bool running_ = false;
    bool stopping_ = false;
    bool batchActive_ = false;
};

WorkerPool::WorkerPool(WorkerPoolConfig config) {
    Start(config);
}

WorkerPool::~WorkerPool() {
    Stop();
}

void WorkerPool::Start(WorkerPoolConfig config) {
    config.workerCount = config.singleThreaded ? 1U : ResolveWorkerCount(config.workerCount);
    if (state_ != nullptr && state_->Running()) {
        const WorkerPoolConfig current = state_->Config();
        if (current.workerCount != config.workerCount || current.pinWorkersToCores != config.pinWorkersToCores || current.firstPinnedCore != config.firstPinnedCore || current.singleThreaded != config.singleThreaded) {
            throw std::logic_error("ECS worker pool cannot be reconfigured while running");
        }
        return;
    }

    auto state = std::make_unique<WorkerPoolState>(config);
    state->Start();
    state_ = std::move(state);
}

void WorkerPool::Stop() {
    if (state_ != nullptr) {
        state_->Stop();
        state_.reset();
    }
}

void WorkerPool::Run(std::span<const WorkerPoolJob> jobs) {
    if (state_ == nullptr) {
        throw std::logic_error("ECS worker pool must be started before running jobs");
    }
    state_->Run(jobs);
}

void WorkerPool::RunBatches(std::span<const WorkerPoolBatch> batches, WorkerPoolBatchCallback callback, void* context) {
    if (state_ == nullptr) {
        throw std::logic_error("ECS worker pool must be started before running batches");
    }
    state_->RunBatches(batches, callback, context);
}

JobHandle WorkerPool::Submit(std::vector<WorkerPoolJob> jobs) {
    if (state_ == nullptr) {
        throw std::logic_error("ECS worker pool must be started before submitting jobs");
    }
    return state_->Submit(std::move(jobs));
}

JobHandle WorkerPool::SubmitBatches(std::vector<WorkerPoolBatch> batches, WorkerPoolBatchCallback callback, void* context) {
    if (state_ == nullptr) {
        throw std::logic_error("ECS worker pool must be started before submitting batches");
    }
    return state_->SubmitBatches(std::move(batches), callback, context);
}

void WorkerPool::ParallelForChunks(std::span<const WorkerPoolChunk> chunks, WorkerPoolChunkCallback callback, void* context) {
    if (state_ == nullptr) {
        throw std::logic_error("ECS worker pool must be started before running chunks");
    }
    state_->ParallelForChunks(chunks, callback, context);
}

JobHandle WorkerPool::SubmitParallelForChunks(std::vector<WorkerPoolChunk> chunks, WorkerPoolChunkCallback callback, void* context) {
    if (chunks.empty()) {
        return {};
    }
    if (callback == nullptr) {
        throw std::invalid_argument("ECS worker pool chunk job must be callable");
    }
    if (state_ == nullptr) {
        throw std::logic_error("ECS worker pool must be started before submitting chunks");
    }
    return state_->SubmitChunks(std::move(chunks), callback, context);
}

void WorkerPool::ParallelForChunks(std::size_t itemCount, std::size_t chunkSize, WorkerPoolChunkCallback callback, void* context) {
    if (itemCount == 0) {
        return;
    }
    if (chunkSize == 0) {
        throw std::invalid_argument("ECS worker pool chunk size must be non-zero");
    }

    std::vector<WorkerPoolChunk> chunks;
    chunks.reserve(((itemCount - 1U) / chunkSize) + 1U);
    for (std::size_t begin = 0; begin < itemCount; begin += chunkSize) {
        const std::size_t chunkIndex = chunks.size();
        chunks.push_back(WorkerPoolChunk{
            .index = chunkIndex,
            .begin = begin,
            .count = std::min(chunkSize, itemCount - begin),
            .preferredWorkerIndex = chunkIndex,
        });
    }

    ParallelForChunks(chunks, callback, context);
}

JobHandle WorkerPool::SubmitParallelForChunks(std::size_t itemCount, std::size_t chunkSize, WorkerPoolChunkCallback callback, void* context) {
    if (itemCount == 0) {
        return {};
    }
    if (chunkSize == 0) {
        throw std::invalid_argument("ECS worker pool chunk size must be non-zero");
    }

    std::vector<WorkerPoolChunk> chunks;
    chunks.reserve(((itemCount - 1U) / chunkSize) + 1U);
    for (std::size_t begin = 0; begin < itemCount; begin += chunkSize) {
        const std::size_t chunkIndex = chunks.size();
        chunks.push_back(WorkerPoolChunk{
            .index = chunkIndex,
            .begin = begin,
            .count = std::min(chunkSize, itemCount - begin),
            .preferredWorkerIndex = chunkIndex,
        });
    }

    return SubmitParallelForChunks(std::move(chunks), callback, context);
}

bool WorkerPool::Running() const noexcept {
    return state_ != nullptr && state_->Running();
}

std::size_t WorkerPool::WorkerCount() const noexcept {
    return state_ == nullptr ? 0 : state_->Config().workerCount;
}

WorkerPoolConfig WorkerPool::Config() const noexcept {
    return state_ == nullptr ? WorkerPoolConfig{} : state_->Config();
}

std::size_t WorkerPool::DefaultWorkerCount() noexcept {
    const std::size_t hardwareThreads = HardwareThreadCount();
    return hardwareThreads <= 1U ? 1U : hardwareThreads - 1U;
}

std::size_t WorkerPool::ResolveWorkerCount(std::size_t requestedWorkerCount) noexcept {
    return requestedWorkerCount == 0 ? DefaultWorkerCount() : requestedWorkerCount;
}

bool WorkerPool::AffinitySupported() noexcept {
#if defined(_WIN32) || defined(__linux__)
    return true;
#else
    return false;
#endif
}

} // namespace kb::ecs
