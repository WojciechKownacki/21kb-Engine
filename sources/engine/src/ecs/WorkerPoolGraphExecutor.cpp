#include "ecs/WorkerPoolGraphExecutor.hpp"
#include "engine/ecs/JobGraph.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <stdexcept>

namespace kb::ecs {
namespace {
class GraphExecution;
thread_local GraphExecution* currentGraph = nullptr;
thread_local WorkerContext currentWorker{};
struct Group { std::size_t remaining = 0U; };
struct Task {
    std::function<void(WorkerContext)> run;
    Group* group = nullptr;
    std::size_t graphIndex = static_cast<std::size_t>(-1);
    std::size_t workerLimit = 0U;
};
class GraphExecution {
public:
    GraphExecution(WorkerPool& owner, JobGraph graph, std::span<const WorkerPoolJob> jobs, std::stop_token stop)
        : pool(owner), graph_(std::move(graph)), jobs_(jobs), stop_(stop) {
        if (graph_.JobCount() != jobs.size() || graph_.HasCycle())
            throw std::invalid_argument("worker graph must be acyclic and match its job count");
        for (const auto& job : jobs) if (!job.callback) throw std::invalid_argument("graph job must be callable");
        for (auto index : graph_.InitialReadyJobs()) EnqueueGraph(index);
    }
    void Pump(WorkerContext worker, Group* group = nullptr) {
        for (;;) {
            Task task;
            bool execute = false;
            {
                std::unique_lock lock(mutex_);
                if (group ? group->remaining == 0U : outstanding_ == 0U) return;
                const auto next = std::find_if(queue_.begin(), queue_.end(), [&](const Task& entry) {
                    const bool eligible = Cancelled() || entry.workerLimit == 0U || worker.workerIndex < entry.workerLimit;
                    return eligible && (!group || entry.graphIndex == static_cast<std::size_t>(-1) || Cancelled());
                });
                if (next == queue_.end()) {
                    changed_.wait_for(lock, std::chrono::milliseconds{1});
                    continue;
                }
                task = std::move(*next);
                queue_.erase(next);
                execute = !Cancelled();
            }
            std::exception_ptr failure;
            if (execute) {
                try {
                    auto context = worker;
                    if (task.workerLimit != 0U) context.workerCount = std::min(context.workerCount, task.workerLimit);
                    task.run(context);
                } catch (...) { failure = std::current_exception(); }
            }
            {
                std::lock_guard lock(mutex_);
                if (failure && !failure_) failure_ = failure;
                if (execute && !failure && task.graphIndex != static_cast<std::size_t>(-1)) {
                    try {
                        const auto successors = graph_.OnCompleted(task.graphIndex);
                        if (!Cancelled()) for (auto index : successors) EnqueueGraph(index);
                    } catch (...) { if (!failure_) failure_ = std::current_exception(); }
                }
                if (task.group) --task.group->remaining;
                --outstanding_;
            }
            changed_.notify_all();
        }
    }
    void Nested(std::vector<Task> tasks) {
        Group group{tasks.size()};
        {
            std::lock_guard lock(mutex_);
            // Reserve all queue entries before publishing the stack-owned group.
            const auto previous = queue_.size();
            try {
                for (auto it = tasks.rbegin(); it != tasks.rend(); ++it) {
                    it->group = &group;
                    queue_.push_front(std::move(*it));
                }
            } catch (...) {
                while (queue_.size() > previous) queue_.pop_front();
                throw;
            }
            outstanding_ += tasks.size();
            nestedJobs_ += tasks.size();
        }
        changed_.notify_all();
        Pump(currentWorker, &group);
        std::exception_ptr failure;
        { std::lock_guard lock(mutex_); failure = failure_; }
        if (failure) std::rethrow_exception(failure);
    }
    WorkerPoolGraphResult Result() const {
        if (failure_) std::rethrow_exception(failure_);
        return {graph_.CompletedCount(), graph_.JobCount() - graph_.CompletedCount(), nestedJobs_};
    }
    WorkerPool& pool;
private:
    bool Cancelled() const { return failure_ != nullptr || stop_.stop_requested(); }
    void EnqueueGraph(std::size_t index) {
        const auto job = jobs_[index];
        queue_.push_back(Task{[job](WorkerContext worker) { job.callback(worker, job.context); }, nullptr, index});
        ++outstanding_;
    }
    JobGraph graph_;
    std::span<const WorkerPoolJob> jobs_;
    std::stop_token stop_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<Task> queue_;
    std::size_t outstanding_ = 0U;
    std::size_t nestedJobs_ = 0U;
    std::exception_ptr failure_;
};
struct GraphBinding {
    GraphExecution* previous = currentGraph;
    WorkerContext previousWorker = currentWorker;
    GraphBinding(GraphExecution& graph, WorkerContext worker) { currentGraph = &graph; currentWorker = worker; }
    ~GraphBinding() { currentGraph = previous; currentWorker = previousWorker; }
};
class LocalGraphBridge final : public detail::GraphBridge {
public:
    bool IsCurrent(const WorkerPool& pool) const noexcept override { return detail::IsExecutingGraphOn(pool); }
    bool RunJobs(WorkerPool& pool, std::span<const WorkerPoolJob> jobs) override {
        return detail::RunNestedJobs(pool, jobs);
    }
    bool RunBatches(WorkerPool& pool, std::span<const WorkerPoolBatch> batches,
        WorkerPoolBatchCallback callback, void* context) override {
        return detail::RunNestedBatches(pool, batches, callback, context);
    }
    bool RunChunks(WorkerPool& pool, std::span<const WorkerPoolChunk> chunks,
        WorkerPoolChunkCallback callback, void* context) override {
        return detail::RunNestedChunks(pool, chunks, callback, context);
    }
};
}

WorkerPoolGraphResult detail::ExecuteGraph(WorkerPool& pool, JobGraph graph, std::span<const WorkerPoolJob> jobs,
    std::stop_token cancellation, GraphBridgeSlot& bridgeSlot, std::mutex& dispatchMutex) {
    if (const auto bridge = bridgeSlot.load(); bridge && bridge->IsCurrent(pool))
        throw std::logic_error("nested graph dispatch is not supported; use synchronous jobs or chunks");
    const std::lock_guard graphLock{dispatchMutex};
    if (!pool.Running()) throw std::logic_error("worker pool must be started before running a graph");
    GraphExecution execution(pool, std::move(graph), jobs, cancellation);
    std::vector<WorkerPoolJob> pumps(pool.WorkerCount(), WorkerPoolJob{
        [](WorkerContext worker, void* context) {
            auto& graph = *static_cast<GraphExecution*>(context);
            GraphBinding binding(graph, worker);
            graph.Pump(worker);
        }, &execution});
    bridgeSlot.store(std::make_shared<LocalGraphBridge>());
    try { pool.Run(pumps); }
    catch (...) { bridgeSlot.store(nullptr); throw; }
    bridgeSlot.store(nullptr);
    return execution.Result();
}

namespace detail {
bool IsExecutingGraphOn(const WorkerPool& pool) noexcept {
    return currentGraph && &currentGraph->pool == &pool;
}
bool RunNestedJobs(WorkerPool& pool, std::span<const WorkerPoolJob> jobs) {
    if (!currentGraph || &currentGraph->pool != &pool) return false;
    std::vector<Task> tasks;
    tasks.reserve(jobs.size());
    for (const auto& job : jobs) {
        if (!job.callback) throw std::invalid_argument("nested job must be callable");
        tasks.push_back(Task{[job](WorkerContext worker) { job.callback(worker, job.context); }});
    }
    currentGraph->Nested(std::move(tasks));
    return true;
}
bool RunNestedBatches(WorkerPool& pool, std::span<const WorkerPoolBatch> batches, WorkerPoolBatchCallback callback, void* context) {
    if (!currentGraph || &currentGraph->pool != &pool) return false;
    if (!callback && !batches.empty()) throw std::invalid_argument("nested batch must be callable");
    std::vector<Task> tasks;
    tasks.reserve(batches.size());
    std::size_t limit = 0U;
    for (const auto& batch : batches) if (batch.workerCountLimit != 0U)
        limit = limit == 0U ? batch.workerCountLimit : std::min(limit, batch.workerCountLimit);
    for (const auto& batch : batches) tasks.push_back(Task{
        [batch, callback, context](WorkerContext worker) { callback(worker, batch, context); },
        nullptr, static_cast<std::size_t>(-1), limit});
    currentGraph->Nested(std::move(tasks));
    return true;
}
bool RunNestedChunks(WorkerPool& pool, std::span<const WorkerPoolChunk> chunks, WorkerPoolChunkCallback callback, void* context) {
    if (!currentGraph || &currentGraph->pool != &pool) return false;
    if (!callback && !chunks.empty()) throw std::invalid_argument("nested chunk must be callable");
    std::vector<Task> tasks;
    tasks.reserve(chunks.size());
    std::size_t limit = 0U;
    for (const auto& chunk : chunks) if (chunk.workerCountLimit != 0U)
        limit = limit == 0U ? chunk.workerCountLimit : std::min(limit, chunk.workerCountLimit);
    for (const auto& chunk : chunks) tasks.push_back(Task{
        [chunk, callback, context](WorkerContext worker) { callback(worker, chunk, context); },
        nullptr, static_cast<std::size_t>(-1), limit});
    currentGraph->Nested(std::move(tasks));
    return true;
}
}
} // namespace kb::ecs
