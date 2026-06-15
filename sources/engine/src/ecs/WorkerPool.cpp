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
                cancelledCompletion = activeCompletion_;
            }
            workers_.clear();
            jobs_ = nullptr;
            jobCount_ = 0;
            nextJob_ = 0;
            batches_ = nullptr;
            batchJob_ = nullptr;
            ownedJobs_.clear();
            ownedBatches_.clear();
            ownedBatchJob_ = nullptr;
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
            cancelledCompletion->Complete(std::make_exception_ptr(std::runtime_error("ECS worker pool stopped before submitted work completed")));
        }
        batchFinished_.notify_all();
    }

    void Run(std::span<const WorkerPoolJob> jobs) {
        if (jobs.empty()) {
            return;
        }

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
    }

    void RunBatches(std::span<const WorkerPoolBatch> batches, const WorkerPoolBatchJob& job) {
        if (batches.empty()) {
            return;
        }
        if (!job) {
            throw std::invalid_argument("ECS worker pool batch job must be callable");
        }

        if (config_.singleThreaded) {
            if (!Running()) {
                throw std::logic_error("ECS worker pool must be started before running batches");
            }
            RunBatchesInline(batches, job);
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
            batchJob_ = &job;
            workerBatchQueues_.assign(config_.workerCount, {});
            for (std::size_t batchIndex = 0; batchIndex < batches.size(); ++batchIndex) {
                const WorkerPoolBatch& batch = batches[batchIndex];
                const std::size_t owner = batch.preferredWorkerIndex == kAnyWorkerPoolWorker ? batchIndex % config_.workerCount : batch.preferredWorkerIndex % config_.workerCount;
                workerBatchQueues_[owner].push_back(batchIndex);
            }

            remainingWork_ = batches.size();
            firstJobException_ = nullptr;
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
    }

    [[nodiscard]] JobHandle Submit(std::vector<WorkerPoolJob> jobs) {
        if (jobs.empty()) {
            return {};
        }

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
            activeCompletion_ = completion;
            workMode_ = WorkMode::Jobs;
            batchActive_ = true;
        }

        batchAvailable_.notify_all();
        return JobHandle{ std::move(completion) };
    }

    [[nodiscard]] JobHandle SubmitBatches(std::vector<WorkerPoolBatch> batches, WorkerPoolBatchJob job) {
        if (batches.empty()) {
            return {};
        }
        if (!job) {
            throw std::invalid_argument("ECS worker pool batch job must be callable");
        }

        if (config_.singleThreaded) {
            if (!Running()) {
                throw std::logic_error("ECS worker pool must be started before submitting batches");
            }
            return SubmitBatchesInline(batches, job);
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
            ownedBatchJob_ = std::move(job);
            batches_ = ownedBatches_.data();
            batchJob_ = &ownedBatchJob_;
            workerBatchQueues_.assign(config_.workerCount, {});
            for (std::size_t batchIndex = 0; batchIndex < ownedBatches_.size(); ++batchIndex) {
                const WorkerPoolBatch& batch = ownedBatches_[batchIndex];
                const std::size_t owner = batch.preferredWorkerIndex == kAnyWorkerPoolWorker ? batchIndex % config_.workerCount : batch.preferredWorkerIndex % config_.workerCount;
                workerBatchQueues_[owner].push_back(batchIndex);
            }

            remainingWork_ = ownedBatches_.size();
            firstJobException_ = nullptr;
            activeCompletion_ = completion;
            workMode_ = WorkMode::Batches;
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
    };

    static void ThrowIfFailed(std::exception_ptr exception) {
        if (exception != nullptr) {
            std::rethrow_exception(exception);
        }
    }

    static void RunJobsInline(std::span<const WorkerPoolJob> jobs) {
        std::exception_ptr firstException;
        const WorkerContext context{
            .workerIndex = 0,
            .workerCount = 1,
        };

        for (const WorkerPoolJob& job : jobs) {
            try {
                job(context);
            } catch (...) {
                if (firstException == nullptr) {
                    firstException = std::current_exception();
                }
            }
        }

        ThrowIfFailed(firstException);
    }

    static void RunBatchesInline(std::span<const WorkerPoolBatch> batches, const WorkerPoolBatchJob& job) {
        std::exception_ptr firstException;
        const WorkerContext context{
            .workerIndex = 0,
            .workerCount = 1,
        };

        for (const WorkerPoolBatch& batch : batches) {
            try {
                job(context, batch);
            } catch (...) {
                if (firstException == nullptr) {
                    firstException = std::current_exception();
                }
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
                job(context);
            } catch (...) {
                if (firstException == nullptr) {
                    firstException = std::current_exception();
                }
            }
        }

        completion->Complete(firstException);
        return JobHandle{ std::move(completion) };
    }

    static JobHandle SubmitBatchesInline(std::span<const WorkerPoolBatch> batches, const WorkerPoolBatchJob& job) {
        auto completion = std::make_shared<JobHandle::State>();
        std::exception_ptr firstException;
        const WorkerContext context{
            .workerIndex = 0,
            .workerCount = 1,
        };

        for (const WorkerPoolBatch& batch : batches) {
            try {
                job(context, batch);
            } catch (...) {
                if (firstException == nullptr) {
                    firstException = std::current_exception();
                }
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

        const WorkerContext context{
            .workerIndex = workerIndex,
            .workerCount = config_.workerCount,
        };

        while (true) {
            const WorkerPoolJob* job = nullptr;
            const WorkerPoolBatchJob* batchJob = nullptr;
            WorkerPoolBatch batch;
            WorkMode mode = WorkMode::None;
            {
                std::unique_lock lock{ mutex_ };
                batchAvailable_.wait(lock, [this, workerIndex] {
                    return stopping_ || HasPendingWorkLocked(workerIndex);
                });

                if (stopping_) {
                    return;
                }

                mode = workMode_;
                if (mode == WorkMode::Jobs) {
                    job = &jobs_[nextJob_];
                    ++nextJob_;
                } else if (mode == WorkMode::Batches) {
                    const std::size_t batchIndex = TakeBatchLocked(workerIndex);
                    batch = batches_[batchIndex];
                    batchJob = batchJob_;
                }
            }

            try {
                if (mode == WorkMode::Jobs) {
                    (*job)(context);
                } else if (mode == WorkMode::Batches) {
                    (*batchJob)(context, batch);
                }
            } catch (...) {
                std::lock_guard lock{ mutex_ };
                if (firstJobException_ == nullptr) {
                    firstJobException_ = std::current_exception();
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
        if (workMode_ == WorkMode::Jobs) {
            return nextJob_ < jobCount_;
        }
        if (workMode_ != WorkMode::Batches) {
            return false;
        }
        if (workerIndex < workerBatchQueues_.size() && !workerBatchQueues_[workerIndex].empty()) {
            return true;
        }
        for (std::size_t index = 0; index < workerBatchQueues_.size(); ++index) {
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
        if (workMode_ != WorkMode::Batches) {
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

        for (std::size_t offset = 1; offset <= workerBatchQueues_.size(); ++offset) {
            const std::size_t victimIndex = (workerIndex + offset) % workerBatchQueues_.size();
            auto& victimQueue = workerBatchQueues_[victimIndex];
            if (!victimQueue.empty()) {
                const std::size_t batchIndex = victimQueue.back();
                victimQueue.pop_back();
                return batchIndex;
            }
        }

        throw std::logic_error("ECS worker pool attempted to take a missing batch");
    }

    void ClearActiveWorkLocked() noexcept {
        jobs_ = nullptr;
        jobCount_ = 0;
        nextJob_ = 0;
        batches_ = nullptr;
        batchJob_ = nullptr;
        ownedJobs_.clear();
        ownedBatches_.clear();
        ownedBatchJob_ = nullptr;
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
    const WorkerPoolBatchJob* batchJob_ = nullptr;
    std::vector<WorkerPoolJob> ownedJobs_;
    std::vector<WorkerPoolBatch> ownedBatches_;
    WorkerPoolBatchJob ownedBatchJob_;
    std::vector<std::deque<std::size_t>> workerBatchQueues_;
    std::size_t remainingWork_ = 0;
    std::size_t startupRemaining_ = 0;
    std::exception_ptr startupException_;
    std::exception_ptr firstJobException_;
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

void WorkerPool::RunBatches(std::span<const WorkerPoolBatch> batches, const WorkerPoolBatchJob& job) {
    if (state_ == nullptr) {
        throw std::logic_error("ECS worker pool must be started before running batches");
    }
    state_->RunBatches(batches, job);
}

JobHandle WorkerPool::Submit(std::vector<WorkerPoolJob> jobs) {
    if (state_ == nullptr) {
        throw std::logic_error("ECS worker pool must be started before submitting jobs");
    }
    return state_->Submit(std::move(jobs));
}

JobHandle WorkerPool::SubmitBatches(std::vector<WorkerPoolBatch> batches, WorkerPoolBatchJob job) {
    if (state_ == nullptr) {
        throw std::logic_error("ECS worker pool must be started before submitting batches");
    }
    return state_->SubmitBatches(std::move(batches), std::move(job));
}

void WorkerPool::ParallelForChunks(std::span<const WorkerPoolChunk> chunks, const WorkerPoolChunkJob& job) {
    if (chunks.empty()) {
        return;
    }
    if (!job) {
        throw std::invalid_argument("ECS worker pool chunk job must be callable");
    }

    std::vector<WorkerPoolBatch> batches;
    batches.reserve(chunks.size());
    for (std::size_t chunkIndex = 0; chunkIndex < chunks.size(); ++chunkIndex) {
        const WorkerPoolChunk& chunk = chunks[chunkIndex];
        batches.push_back(WorkerPoolBatch{
            .index = chunkIndex,
            .begin = chunk.begin,
            .count = chunk.count,
            .preferredWorkerIndex = chunk.preferredWorkerIndex,
        });
    }

    const WorkerPoolBatchJob batchJob = [&chunks, &job](WorkerContext context, const WorkerPoolBatch& batch) {
        const WorkerPoolChunk& chunk = chunks[batch.index];
        job(context, chunk);
    };
    RunBatches(batches, batchJob);
}

JobHandle WorkerPool::SubmitParallelForChunks(std::vector<WorkerPoolChunk> chunks, WorkerPoolChunkJob job) {
    if (chunks.empty()) {
        return {};
    }
    if (!job) {
        throw std::invalid_argument("ECS worker pool chunk job must be callable");
    }

    std::vector<WorkerPoolBatch> batches;
    batches.reserve(chunks.size());
    for (std::size_t chunkIndex = 0; chunkIndex < chunks.size(); ++chunkIndex) {
        const WorkerPoolChunk& chunk = chunks[chunkIndex];
        batches.push_back(WorkerPoolBatch{
            .index = chunkIndex,
            .begin = chunk.begin,
            .count = chunk.count,
            .preferredWorkerIndex = chunk.preferredWorkerIndex,
        });
    }

    return SubmitBatches(std::move(batches), [chunks = std::move(chunks), job = std::move(job)](WorkerContext context, const WorkerPoolBatch& batch) {
        const WorkerPoolChunk& chunk = chunks[batch.index];
        job(context, chunk);
    });
}

void WorkerPool::ParallelForChunks(std::size_t itemCount, std::size_t chunkSize, const WorkerPoolChunkJob& job) {
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

    ParallelForChunks(chunks, job);
}

JobHandle WorkerPool::SubmitParallelForChunks(std::size_t itemCount, std::size_t chunkSize, WorkerPoolChunkJob job) {
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

    return SubmitParallelForChunks(std::move(chunks), std::move(job));
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
