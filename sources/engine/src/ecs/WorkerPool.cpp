#include "engine/ecs/WorkerPool.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
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
#if defined(__ANDROID__)
    const int result = sched_setaffinity(0, sizeof(set), &set);
    const int errorCode = errno;
#else
    const int result = pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
#endif
    if (result != 0) {
#if !defined(__ANDROID__)
        const int errorCode = result;
#endif
        throw std::runtime_error("ECS worker affinity failed: " + std::string{ std::strerror(errorCode) });
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
    using DispatchClock = std::chrono::steady_clock;
    using DispatchTimePoint = DispatchClock::time_point;

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
            batchCount_ = 0;
            chunks_ = nullptr;
            chunkCount_ = 0;
            batchCallback_ = nullptr;
            batchCallbackContext_ = nullptr;
            chunkCallback_ = nullptr;
            chunkCallbackContext_ = nullptr;
            ownedJobs_.clear();
            ownedBatches_.clear();
            ownedChunks_.clear();
            workerBatchQueues_.clear();
            workerBatchQueueCursors_.clear();
            staticStridedWorkerDone_.clear();
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

        const DispatchTimePoint dispatchStartedAt = BeginDispatchScheduleTiming();
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
            RecordDispatchLocked(workMode_, jobs.size(), activeWorkerLimit_);
            batchActive_ = true;
        }

        batchAvailable_.notify_all();
        EndDispatchScheduleTiming(dispatchStartedAt);

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

        const DispatchTimePoint dispatchStartedAt = BeginDispatchScheduleTiming();
        {
            std::unique_lock lock{ mutex_ };
            if (!running_) {
                throw std::logic_error("ECS worker pool must be started before running batches");
            }
            batchFinished_.wait(lock, [this] {
                return !batchActive_;
            });

            batches_ = batches.data();
            batchCount_ = batches.size();
            batchCallback_ = callback;
            batchCallbackContext_ = context;
            activeWorkerLimit_ = ResolveBatchWorkerLimit(batches);
            const bool useStaticStridedBatches = CanRunBatchesStaticStrided(batches);
            if (useStaticStridedBatches) {
                PrepareStaticStridedWorkLocked(batches.size());
            } else {
                PrepareWorkerBatchQueuesLocked(batches.size());
                for (std::size_t batchIndex = 0; batchIndex < batches.size(); ++batchIndex) {
                    const WorkerPoolBatch& batch = batches[batchIndex];
                    const std::size_t owner = batch.preferredWorkerIndex == kAnyWorkerPoolWorker ? batchIndex % activeWorkerLimit_ : batch.preferredWorkerIndex % activeWorkerLimit_;
                    workerBatchQueues_[owner].push_back(batchIndex);
                }
            }

            remainingWork_ = useStaticStridedBatches ? activeWorkerLimit_ : batches.size();
            firstJobException_ = nullptr;
            cancelledRunException_ = nullptr;
            workMode_ = useStaticStridedBatches ? WorkMode::BatchesStaticStrided : WorkMode::Batches;
            RecordDispatchLocked(workMode_, batches.size(), activeWorkerLimit_);
            batchActive_ = true;
        }

        batchAvailable_.notify_all();
        EndDispatchScheduleTiming(dispatchStartedAt);

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

        const DispatchTimePoint dispatchStartedAt = BeginDispatchScheduleTiming();
        {
            std::unique_lock lock{ mutex_ };
            if (!running_) {
                throw std::logic_error("ECS worker pool must be started before running chunks");
            }
            batchFinished_.wait(lock, [this] {
                return !batchActive_;
            });

            chunks_ = chunks.data();
            chunkCount_ = chunks.size();
            chunkCallback_ = callback;
            chunkCallbackContext_ = context;
            activeWorkerLimit_ = ResolveChunkWorkerLimit(chunks);
            const bool useStaticStridedChunks = CanRunChunksStaticStrided(chunks);
            if (useStaticStridedChunks) {
                PrepareStaticStridedWorkLocked(chunks.size());
            } else {
                PrepareWorkerBatchQueuesLocked(chunks.size());
                for (std::size_t chunkIndex = 0; chunkIndex < chunks.size(); ++chunkIndex) {
                    const WorkerPoolChunk& chunk = chunks[chunkIndex];
                    const std::size_t owner = chunk.preferredWorkerIndex == kAnyWorkerPoolWorker ? chunkIndex % activeWorkerLimit_ : chunk.preferredWorkerIndex % activeWorkerLimit_;
                    workerBatchQueues_[owner].push_back(chunkIndex);
                }
            }

            remainingWork_ = useStaticStridedChunks ? activeWorkerLimit_ : chunks.size();
            firstJobException_ = nullptr;
            cancelledRunException_ = nullptr;
            workMode_ = useStaticStridedChunks ? WorkMode::ChunksStaticStrided : WorkMode::Chunks;
            RecordDispatchLocked(workMode_, chunks.size(), activeWorkerLimit_);
            batchActive_ = true;
        }

        batchAvailable_.notify_all();
        EndDispatchScheduleTiming(dispatchStartedAt);

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
            RecordDispatchLocked(workMode_, ownedJobs_.size(), activeWorkerLimit_);
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
            batchCount_ = ownedBatches_.size();
            batchCallback_ = callback;
            batchCallbackContext_ = context;
            activeWorkerLimit_ = ResolveBatchWorkerLimit(ownedBatches_);
            const bool useStaticStridedBatches = CanRunBatchesStaticStrided(ownedBatches_);
            if (useStaticStridedBatches) {
                PrepareStaticStridedWorkLocked(ownedBatches_.size());
            } else {
                PrepareWorkerBatchQueuesLocked(ownedBatches_.size());
                for (std::size_t batchIndex = 0; batchIndex < ownedBatches_.size(); ++batchIndex) {
                    const WorkerPoolBatch& batch = ownedBatches_[batchIndex];
                    const std::size_t owner = batch.preferredWorkerIndex == kAnyWorkerPoolWorker ? batchIndex % activeWorkerLimit_ : batch.preferredWorkerIndex % activeWorkerLimit_;
                    workerBatchQueues_[owner].push_back(batchIndex);
                }
            }

            remainingWork_ = useStaticStridedBatches ? activeWorkerLimit_ : ownedBatches_.size();
            firstJobException_ = nullptr;
            cancelledRunException_ = nullptr;
            activeCompletion_ = completion;
            workMode_ = useStaticStridedBatches ? WorkMode::BatchesStaticStrided : WorkMode::Batches;
            RecordDispatchLocked(workMode_, ownedBatches_.size(), activeWorkerLimit_);
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
            chunkCount_ = ownedChunks_.size();
            chunkCallback_ = callback;
            chunkCallbackContext_ = context;
            activeWorkerLimit_ = ResolveChunkWorkerLimit(ownedChunks_);
            const bool useStaticStridedChunks = CanRunChunksStaticStrided(ownedChunks_);
            if (useStaticStridedChunks) {
                PrepareStaticStridedWorkLocked(ownedChunks_.size());
            } else {
                PrepareWorkerBatchQueuesLocked(ownedChunks_.size());
                for (std::size_t chunkIndex = 0; chunkIndex < ownedChunks_.size(); ++chunkIndex) {
                    const WorkerPoolChunk& chunk = ownedChunks_[chunkIndex];
                    const std::size_t owner = chunk.preferredWorkerIndex == kAnyWorkerPoolWorker ? chunkIndex % activeWorkerLimit_ : chunk.preferredWorkerIndex % activeWorkerLimit_;
                    workerBatchQueues_[owner].push_back(chunkIndex);
                }
            }

            remainingWork_ = useStaticStridedChunks ? activeWorkerLimit_ : ownedChunks_.size();
            firstJobException_ = nullptr;
            cancelledRunException_ = nullptr;
            activeCompletion_ = completion;
            workMode_ = useStaticStridedChunks ? WorkMode::ChunksStaticStrided : WorkMode::Chunks;
            RecordDispatchLocked(workMode_, ownedChunks_.size(), activeWorkerLimit_);
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

    [[nodiscard]] WorkerPoolDispatchTelemetry DispatchTelemetry() const noexcept {
        std::lock_guard lock{ mutex_ };
        return telemetry_;
    }

private:
    enum class WorkMode {
        None,
        Jobs,
        Batches,
        BatchesStaticStrided,
        Chunks,
        ChunksStaticStrided,
    };

    [[nodiscard]] static WorkerPoolDispatchMode PublicWorkMode(WorkMode mode) noexcept {
        switch (mode) {
        case WorkMode::Jobs:
            return WorkerPoolDispatchMode::Jobs;
        case WorkMode::Batches:
            return WorkerPoolDispatchMode::Batches;
        case WorkMode::BatchesStaticStrided:
            return WorkerPoolDispatchMode::BatchesStaticStrided;
        case WorkMode::Chunks:
            return WorkerPoolDispatchMode::Chunks;
        case WorkMode::ChunksStaticStrided:
            return WorkerPoolDispatchMode::ChunksStaticStrided;
        case WorkMode::None:
            break;
        }
        return WorkerPoolDispatchMode::None;
    }

    void RecordDispatchLocked(WorkMode mode, std::size_t workItemCount, std::size_t queueOwnerCount) noexcept {
        if (!config_.collectDispatchTelemetry) {
            return;
        }
        telemetry_.lastMode = PublicWorkMode(mode);
        ++telemetry_.dispatchCount;
        if (mode == WorkMode::BatchesStaticStrided || mode == WorkMode::ChunksStaticStrided) {
            ++telemetry_.staticStridedDispatchCount;
        } else if (mode == WorkMode::Batches || mode == WorkMode::Chunks) {
            ++telemetry_.queuedDispatchCount;
        }
        telemetry_.lastWorkItemCount = workItemCount;
        telemetry_.lastActiveWorkerCount = activeWorkerLimit_;
        telemetry_.lastConfiguredWorkerCount = config_.workerCount;
        telemetry_.lastQueueOwnerCount = queueOwnerCount;
        telemetry_.lastStealCount = 0;
        telemetry_.lastDispatchWallNanoseconds = 0;
        telemetry_.lastWorkerActiveNanoseconds = 0;
        telemetry_.lastWorkerCapacityNanoseconds = 0;
        telemetry_.lastWorkerUtilizationPercent = 0.0;
        activeDispatchStartedAt_ = DispatchClock::now();
    }

    [[nodiscard]] DispatchTimePoint BeginDispatchScheduleTiming() const noexcept {
        return config_.collectDispatchTelemetry ? DispatchClock::now() : DispatchTimePoint{};
    }

    void EndDispatchScheduleTiming(DispatchTimePoint startedAt) noexcept {
        if (!config_.collectDispatchTelemetry || startedAt == DispatchTimePoint{}) {
            return;
        }

        std::uint64_t elapsedNanoseconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(DispatchClock::now() - startedAt).count());
        if (elapsedNanoseconds == 0U) {
            elapsedNanoseconds = 1U;
        }

        std::lock_guard lock{ mutex_ };
        telemetry_.lastDispatchScheduleNanoseconds = elapsedNanoseconds;
        telemetry_.totalDispatchScheduleNanoseconds += elapsedNanoseconds;
        telemetry_.averageDispatchScheduleNanoseconds =
            telemetry_.dispatchCount == 0U ? 0U : telemetry_.totalDispatchScheduleNanoseconds / telemetry_.dispatchCount;
    }

    [[nodiscard]] DispatchTimePoint BeginWorkerActiveTiming() const noexcept {
        return config_.collectDispatchTelemetry ? DispatchClock::now() : DispatchTimePoint{};
    }

    [[nodiscard]] std::uint64_t EndWorkerActiveTiming(DispatchTimePoint startedAt) const noexcept {
        if (!config_.collectDispatchTelemetry || startedAt == DispatchTimePoint{}) {
            return 0U;
        }

        std::uint64_t elapsedNanoseconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(DispatchClock::now() - startedAt).count());
        return elapsedNanoseconds == 0U ? 1U : elapsedNanoseconds;
    }

    void RecordWorkerActiveTimeLocked(std::uint64_t elapsedNanoseconds) noexcept {
        if (!config_.collectDispatchTelemetry || elapsedNanoseconds == 0U) {
            return;
        }

        telemetry_.lastWorkerActiveNanoseconds += elapsedNanoseconds;
        telemetry_.totalWorkerActiveNanoseconds += elapsedNanoseconds;
    }

    void RecordWorkerStealLocked() noexcept {
        if (!config_.collectDispatchTelemetry) {
            return;
        }

        ++telemetry_.lastStealCount;
        ++telemetry_.totalStealCount;
    }

    void CompleteDispatchTelemetryLocked() noexcept {
        if (!config_.collectDispatchTelemetry || activeDispatchStartedAt_ == DispatchTimePoint{}) {
            return;
        }

        std::uint64_t wallNanoseconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(DispatchClock::now() - activeDispatchStartedAt_).count());
        if (wallNanoseconds == 0U) {
            wallNanoseconds = 1U;
        }

        const std::uint64_t activeWorkerCount = static_cast<std::uint64_t>(std::max<std::size_t>(telemetry_.lastActiveWorkerCount, 1U));
        const std::uint64_t capacityNanoseconds = wallNanoseconds * activeWorkerCount;
        telemetry_.lastDispatchWallNanoseconds = wallNanoseconds;
        telemetry_.totalDispatchWallNanoseconds += wallNanoseconds;
        telemetry_.averageDispatchWallNanoseconds =
            telemetry_.dispatchCount == 0U ? 0U : telemetry_.totalDispatchWallNanoseconds / telemetry_.dispatchCount;
        telemetry_.lastWorkerCapacityNanoseconds = capacityNanoseconds;
        telemetry_.totalWorkerCapacityNanoseconds += capacityNanoseconds;
        telemetry_.averageWorkerActiveNanoseconds =
            telemetry_.dispatchCount == 0U ? 0U : telemetry_.totalWorkerActiveNanoseconds / telemetry_.dispatchCount;
        telemetry_.lastWorkerUtilizationPercent = capacityNanoseconds == 0U
            ? 0.0
            : std::min(100.0, (static_cast<double>(telemetry_.lastWorkerActiveNanoseconds) * 100.0) / static_cast<double>(capacityNanoseconds));
        telemetry_.averageWorkerUtilizationPercent = telemetry_.totalWorkerCapacityNanoseconds == 0U
            ? 0.0
            : std::min(100.0, (static_cast<double>(telemetry_.totalWorkerActiveNanoseconds) * 100.0) / static_cast<double>(telemetry_.totalWorkerCapacityNanoseconds));
        activeDispatchStartedAt_ = DispatchTimePoint{};
    }

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

    static bool CanRunBatchesStaticStrided(std::span<const WorkerPoolBatch> batches) noexcept {
        for (std::size_t batchIndex = 0; batchIndex < batches.size(); ++batchIndex) {
            const std::size_t preferredWorkerIndex = batches[batchIndex].preferredWorkerIndex;
            if (preferredWorkerIndex != kAnyWorkerPoolWorker && preferredWorkerIndex != batchIndex) {
                return false;
            }
        }
        return true;
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

    static bool CanRunChunksStaticStrided(std::span<const WorkerPoolChunk> chunks) noexcept {
        for (std::size_t chunkIndex = 0; chunkIndex < chunks.size(); ++chunkIndex) {
            const std::size_t preferredWorkerIndex = chunks[chunkIndex].preferredWorkerIndex;
            if (preferredWorkerIndex != kAnyWorkerPoolWorker && preferredWorkerIndex != chunkIndex) {
                return false;
            }
        }
        return true;
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
            const WorkerPoolBatch* staticBatches = nullptr;
            std::size_t staticBatchCount = 0;
            const WorkerPoolChunk* staticChunks = nullptr;
            std::size_t staticChunkCount = 0;
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
                } else if (mode == WorkMode::BatchesStaticStrided) {
                    staticBatches = batches_;
                    staticBatchCount = batchCount_;
                    batchCallback = batchCallback_;
                    batchCallbackContext = batchCallbackContext_;
                } else if (mode == WorkMode::Chunks) {
                    const std::size_t chunkIndex = TakeBatchLocked(workerIndex);
                    chunk = chunks_[chunkIndex];
                    chunkCallback = chunkCallback_;
                    chunkCallbackContext = chunkCallbackContext_;
                } else if (mode == WorkMode::ChunksStaticStrided) {
                    staticChunks = chunks_;
                    staticChunkCount = chunkCount_;
                    chunkCallback = chunkCallback_;
                    chunkCallbackContext = chunkCallbackContext_;
                }
            }

            const WorkerContext context{
                .workerIndex = workerIndex,
                .workerCount = workerCount,
            };

            const DispatchTimePoint activeStartedAt = BeginWorkerActiveTiming();
            try {
                if (mode == WorkMode::Jobs) {
                    job->callback(context, job->context);
                } else if (mode == WorkMode::Batches) {
                    batchCallback(context, batch, batchCallbackContext);
                } else if (mode == WorkMode::BatchesStaticStrided) {
                    for (std::size_t batchIndex = workerIndex; batchIndex < staticBatchCount; batchIndex += workerCount) {
                        batchCallback(context, staticBatches[batchIndex], batchCallbackContext);
                    }
                } else if (mode == WorkMode::Chunks) {
                    chunkCallback(context, chunk, chunkCallbackContext);
                } else if (mode == WorkMode::ChunksStaticStrided) {
                    for (std::size_t chunkIndex = workerIndex; chunkIndex < staticChunkCount; chunkIndex += workerCount) {
                        chunkCallback(context, staticChunks[chunkIndex], chunkCallbackContext);
                    }
                }
            } catch (...) {
                std::lock_guard lock{ mutex_ };
                if (firstJobException_ == nullptr) {
                    firstJobException_ = std::current_exception();
                    if (mode != WorkMode::BatchesStaticStrided && mode != WorkMode::ChunksStaticStrided) {
                        CancelPendingWorkLocked();
                    }
                }
            }
            const std::uint64_t activeNanoseconds = EndWorkerActiveTiming(activeStartedAt);

            {
                std::lock_guard lock{ mutex_ };
                RecordWorkerActiveTimeLocked(activeNanoseconds);
                if ((mode == WorkMode::BatchesStaticStrided || mode == WorkMode::ChunksStaticStrided) && workerIndex < staticStridedWorkerDone_.size()) {
                    staticStridedWorkerDone_[workerIndex] = 1U;
                }
                --remainingWork_;
                if (remainingWork_ == 0) {
                    CompleteDispatchTelemetryLocked();
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
        if (workMode_ == WorkMode::BatchesStaticStrided || workMode_ == WorkMode::ChunksStaticStrided) {
            return workerIndex < activeWorkerLimit_
                && workerIndex < staticStridedWorkerDone_.size()
                && staticStridedWorkerDone_[workerIndex] == 0U;
        }
        if (workMode_ != WorkMode::Batches && workMode_ != WorkMode::Chunks) {
            return false;
        }
        if (QueueHasPendingLocked(workerIndex)) {
            return true;
        }
        for (std::size_t index = 0; index < activeWorkerLimit_; ++index) {
            if (index != workerIndex && QueueHasPendingLocked(index)) {
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
        if (workMode_ == WorkMode::BatchesStaticStrided || workMode_ == WorkMode::ChunksStaticStrided) {
            for (std::size_t index = 0; index < activeWorkerLimit_ && index < staticStridedWorkerDone_.size(); ++index) {
                if (staticStridedWorkerDone_[index] == 0U) {
                    return true;
                }
            }
            return false;
        }
        if (workMode_ != WorkMode::Batches && workMode_ != WorkMode::Chunks) {
            return false;
        }
        for (std::size_t index = 0; index < activeWorkerLimit_; ++index) {
            if (QueueHasPendingLocked(index)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::size_t TakeBatchLocked(std::size_t workerIndex) {
        auto& localQueue = workerBatchQueues_[workerIndex];
        std::size_t& localCursor = workerBatchQueueCursors_[workerIndex];
        if (localCursor < localQueue.size()) {
            const std::size_t batchIndex = localQueue[localCursor];
            ++localCursor;
            return batchIndex;
        }

        for (std::size_t offset = 1; offset <= activeWorkerLimit_; ++offset) {
            const std::size_t victimIndex = (workerIndex + offset) % activeWorkerLimit_;
            auto& victimQueue = workerBatchQueues_[victimIndex];
            const std::size_t victimCursor = workerBatchQueueCursors_[victimIndex];
            if (victimCursor < victimQueue.size()) {
                const std::size_t batchIndex = victimQueue.back();
                victimQueue.pop_back();
                RecordWorkerStealLocked();
                return batchIndex;
            }
        }

        throw std::logic_error("ECS worker pool attempted to take a missing batch");
    }

    void PrepareWorkerBatchQueuesLocked(std::size_t workCount) {
        if (workerBatchQueues_.size() != config_.workerCount) {
            workerBatchQueues_.resize(config_.workerCount);
        }
        if (workerBatchQueueCursors_.size() != config_.workerCount) {
            workerBatchQueueCursors_.resize(config_.workerCount);
        }

        const std::size_t reservePerActiveWorker = activeWorkerLimit_ == 0U
            ? 0U
            : ((workCount + activeWorkerLimit_ - 1U) / activeWorkerLimit_);
        for (std::size_t workerIndex = 0; workerIndex < config_.workerCount; ++workerIndex) {
            workerBatchQueues_[workerIndex].clear();
            workerBatchQueueCursors_[workerIndex] = 0U;
            if (workerIndex < activeWorkerLimit_ && workerBatchQueues_[workerIndex].capacity() < reservePerActiveWorker) {
                workerBatchQueues_[workerIndex].reserve(reservePerActiveWorker);
            }
        }
    }

    void PrepareStaticStridedWorkLocked(std::size_t workCount) {
        activeWorkerLimit_ = std::min<std::size_t>(activeWorkerLimit_, std::max<std::size_t>(workCount, 1U));
        if (staticStridedWorkerDone_.size() != config_.workerCount) {
            staticStridedWorkerDone_.resize(config_.workerCount);
        }
        std::fill(staticStridedWorkerDone_.begin(), staticStridedWorkerDone_.end(), 1U);
        std::fill_n(staticStridedWorkerDone_.begin(), activeWorkerLimit_, 0U);
    }

    void ResetWorkerBatchQueuesLocked() noexcept {
        for (std::vector<std::size_t>& queue : workerBatchQueues_) {
            queue.clear();
        }
        std::fill(workerBatchQueueCursors_.begin(), workerBatchQueueCursors_.end(), 0U);
    }

    [[nodiscard]] std::size_t PendingInQueueLocked(std::size_t workerIndex) const noexcept {
        if (workerIndex >= workerBatchQueues_.size() || workerIndex >= workerBatchQueueCursors_.size()) {
            return 0U;
        }
        const std::vector<std::size_t>& queue = workerBatchQueues_[workerIndex];
        const std::size_t cursor = std::min(workerBatchQueueCursors_[workerIndex], queue.size());
        return queue.size() - cursor;
    }

    [[nodiscard]] bool QueueHasPendingLocked(std::size_t workerIndex) const noexcept {
        return PendingInQueueLocked(workerIndex) != 0U;
    }

    void CancelPendingWorkLocked() noexcept {
        std::size_t pendingWork = 0;
        if (workMode_ == WorkMode::Jobs) {
            pendingWork = jobCount_ > nextJob_ ? jobCount_ - nextJob_ : 0U;
            nextJob_ = jobCount_;
        } else if (workMode_ == WorkMode::Batches || workMode_ == WorkMode::Chunks) {
            for (std::size_t index = 0; index < activeWorkerLimit_; ++index) {
                pendingWork += PendingInQueueLocked(index);
            }
            ResetWorkerBatchQueuesLocked();
        } else if (workMode_ == WorkMode::BatchesStaticStrided || workMode_ == WorkMode::ChunksStaticStrided) {
            for (std::size_t index = 0; index < activeWorkerLimit_ && index < staticStridedWorkerDone_.size(); ++index) {
                if (staticStridedWorkerDone_[index] == 0U) {
                    ++pendingWork;
                    staticStridedWorkerDone_[index] = 1U;
                }
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
        batchCount_ = 0;
        chunks_ = nullptr;
        chunkCount_ = 0;
        batchCallback_ = nullptr;
        batchCallbackContext_ = nullptr;
        chunkCallback_ = nullptr;
        chunkCallbackContext_ = nullptr;
        activeWorkerLimit_ = config_.workerCount;
        ownedJobs_.clear();
        ownedBatches_.clear();
        ownedChunks_.clear();
        ResetWorkerBatchQueuesLocked();
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
    std::size_t batchCount_ = 0;
    const WorkerPoolChunk* chunks_ = nullptr;
    std::size_t chunkCount_ = 0;
    WorkerPoolBatchCallback batchCallback_ = nullptr;
    void* batchCallbackContext_ = nullptr;
    WorkerPoolChunkCallback chunkCallback_ = nullptr;
    void* chunkCallbackContext_ = nullptr;
    std::vector<WorkerPoolJob> ownedJobs_;
    std::vector<WorkerPoolBatch> ownedBatches_;
    std::vector<WorkerPoolChunk> ownedChunks_;
    std::vector<std::vector<std::size_t>> workerBatchQueues_;
    std::vector<std::size_t> workerBatchQueueCursors_;
    std::vector<unsigned char> staticStridedWorkerDone_;
    WorkerPoolDispatchTelemetry telemetry_;
    DispatchTimePoint activeDispatchStartedAt_{};
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
        if (current.workerCount != config.workerCount ||
            current.pinWorkersToCores != config.pinWorkersToCores ||
            current.firstPinnedCore != config.firstPinnedCore ||
            current.singleThreaded != config.singleThreaded ||
            current.collectDispatchTelemetry != config.collectDispatchTelemetry) {
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

WorkerPoolDispatchTelemetry WorkerPool::DispatchTelemetry() const noexcept {
    return state_ == nullptr ? WorkerPoolDispatchTelemetry{} : state_->DispatchTelemetry();
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
