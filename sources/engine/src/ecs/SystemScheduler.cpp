#include "engine/ecs/SystemScheduler.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <queue>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace kb::ecs {
namespace {

[[nodiscard]] std::uint64_t ToNanoseconds(std::chrono::steady_clock::duration duration) noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
}

struct SystemExecutionSample {
    std::size_t systemIndex = 0;
    std::size_t workerIndex = 0;
    std::uint64_t jobsCount = 0;
    std::uint64_t startTimeNanoseconds = 0;
    std::uint64_t endTimeNanoseconds = 0;
    SystemProfilerCounters profilerCounters;
};

[[nodiscard]] std::string_view WorkerDispatchModeTraceName(WorkerPoolDispatchMode mode) noexcept {
    switch (mode) {
    case WorkerPoolDispatchMode::Jobs:
        return "jobs";
    case WorkerPoolDispatchMode::Batches:
        return "batches";
    case WorkerPoolDispatchMode::BatchesStaticStrided:
        return "batches_static_strided";
    case WorkerPoolDispatchMode::Chunks:
        return "chunks";
    case WorkerPoolDispatchMode::ChunksStaticStrided:
        return "chunks_static_strided";
    case WorkerPoolDispatchMode::None:
        break;
    }
    return "none";
}

} // namespace

SystemScheduler::SystemScheduler(SystemSchedulerConfig config) noexcept
    : schedulingMode_(config.mode)
    , debugTraceEnabled_(config.debugTraceEnabled)
    , profilerEnabled_(config.profilerEnabled)
    , parallelExecutionEnabled_(config.parallelExecutionEnabled)
    , parallelismMode_(config.parallelismMode)
    , runtimeAccessValidationEnabled_(config.runtimeAccessValidationEnabled)
    , workerPoolConfig_(config.workerPool) {}

SystemScheduler::~SystemScheduler() = default;

void SystemScheduler::Add(std::unique_ptr<System> system, World& world) {
    if (system == nullptr) {
        throw std::invalid_argument("ECS system scheduler cannot add a null system");
    }

    SystemAccess access = system->DeclareAccess(world);
    system->OnCreate(world);
    systems_.push_back(ScheduledSystem{
        .system = std::move(system),
        .access = std::move(access),
    });
    graphDirty_ = true;
}

void SystemScheduler::Update(World& world, float deltaSeconds) {
    if (graphDirty_) {
        RebuildExecutionOrder();
    }

    std::vector<std::uint64_t> systemEndTimes;
    std::chrono::steady_clock::time_point frameStart;
    const bool instrumentationEnabled = InstrumentationEnabled();
    if (instrumentationEnabled) {
        BeginProfilerTrace();
        systemEndTimes.assign(systems_.size(), 0);
        frameStart = std::chrono::steady_clock::now();
    }

    std::vector<SystemExecutionSample> executionSamples;
    for (std::size_t stageIndex = 0; stageIndex < executionStages_.size(); ++stageIndex) {
        const ExecutionStage& stage = executionStages_[stageIndex];
        const bool runStageInParallel = ShouldRunStageInParallel(stage);
        if (runStageInParallel) {
            if (instrumentationEnabled) {
                ++lastTrace_.frameCounters.parallelStageCount;
            }
            executionSamples.assign(stage.systems.size(), {});
            std::vector<WorkerPoolBatch> batches;
            batches.reserve(stage.systems.size());
            for (std::size_t slot = 0; slot < stage.systems.size(); ++slot) {
                batches.push_back(WorkerPoolBatch{
                    .index = slot,
                    .begin = slot,
                    .count = 1,
                    .preferredWorkerIndex = kAnyWorkerPoolWorker,
                });
            }

            auto stageJob = [this, &world, deltaSeconds, &stage, frameStart, &systemEndTimes, &executionSamples, instrumentationEnabled](WorkerContext context, const WorkerPoolBatch& batch) {
                const std::size_t slot = batch.index;
                const std::size_t systemIndex = stage.systems[slot];
                std::chrono::steady_clock::time_point systemStart;
                if (instrumentationEnabled) {
                    systems_[systemIndex].system->ResetProfilerCounters();
                    systemStart = std::chrono::steady_clock::now();
                }
                RuntimeAccessValidator::Guard accessGuard = runtimeAccessValidationEnabled_
                    ? accessValidator_.Acquire(systems_[systemIndex].system->Name(), systems_[systemIndex].access, context.workerIndex)
                    : RuntimeAccessValidator::Guard{};
                systems_[systemIndex].system->SetExecutionWorkerPool(nullptr);
                systems_[systemIndex].system->OnUpdate(world, deltaSeconds);
                if (instrumentationEnabled) {
                    const std::chrono::steady_clock::time_point systemEnd = std::chrono::steady_clock::now();
                    const std::uint64_t startTimeNanoseconds = ToNanoseconds(systemStart - frameStart);
                    const std::uint64_t endTimeNanoseconds = ToNanoseconds(systemEnd - frameStart);
                    systemEndTimes[systemIndex] = endTimeNanoseconds;
                    executionSamples[slot] = SystemExecutionSample{
                        .systemIndex = systemIndex,
                        .workerIndex = context.workerIndex,
                        .jobsCount = 1,
                        .startTimeNanoseconds = startTimeNanoseconds,
                        .endTimeNanoseconds = endTimeNanoseconds,
                        .profilerCounters = systems_[systemIndex].system->ProfilerCounters(),
                    };
                }
            };
            RuntimeWorkerPool().RunBatches(batches, stageJob);

            if (instrumentationEnabled) {
                for (const SystemExecutionSample& sample : executionSamples) {
                    TraceSystemExecution(
                        sample.systemIndex,
                        stageIndex,
                        sample.workerIndex,
                        sample.jobsCount,
                        sample.startTimeNanoseconds,
                        sample.endTimeNanoseconds,
                        sample.profilerCounters,
                        systemEndTimes,
                        reverseDependencyGraph_);
                }
            }
        } else {
            for (std::size_t systemIndex : stage.systems) {
                std::chrono::steady_clock::time_point systemStart;
                if (instrumentationEnabled) {
                    systems_[systemIndex].system->ResetProfilerCounters();
                    systemStart = std::chrono::steady_clock::now();
                }
                RuntimeAccessValidator::Guard accessGuard = runtimeAccessValidationEnabled_
                    ? accessValidator_.Acquire(systems_[systemIndex].system->Name(), systems_[systemIndex].access, 0)
                    : RuntimeAccessValidator::Guard{};
                WorkerPool* queryWorkerPool = parallelExecutionEnabled_ && parallelismMode_ == SystemSchedulerParallelismMode::QueryChunks && !workerPoolConfig_.singleThreaded
                    ? &RuntimeWorkerPool()
                    : nullptr;
                systems_[systemIndex].system->SetExecutionWorkerPool(queryWorkerPool);
                systems_[systemIndex].system->OnUpdate(world, deltaSeconds);
                if (instrumentationEnabled) {
                    const std::chrono::steady_clock::time_point systemEnd = std::chrono::steady_clock::now();
                    const std::uint64_t startTimeNanoseconds = ToNanoseconds(systemStart - frameStart);
                    const std::uint64_t endTimeNanoseconds = ToNanoseconds(systemEnd - frameStart);
                    systemEndTimes[systemIndex] = endTimeNanoseconds;
                    TraceSystemExecution(systemIndex, stageIndex, 0, 1, startTimeNanoseconds, endTimeNanoseconds, systems_[systemIndex].system->ProfilerCounters(), systemEndTimes, reverseDependencyGraph_);
                }
            }
        }
    }

    if (instrumentationEnabled) {
        EndProfilerTrace(ToNanoseconds(std::chrono::steady_clock::now() - frameStart));
    }
}

void SystemScheduler::Shutdown(World& world) {
    for (auto it = systems_.rbegin(); it != systems_.rend(); ++it) {
        it->system->OnDestroy(world);
    }
    systems_.clear();
    executionOrder_.clear();
    executionStages_.clear();
    reverseDependencyGraph_.clear();
    traceCounterIndexBySystem_.clear();
    workerPool_.reset();
    accessValidator_.Clear();
    graphDirty_ = false;
    lastTrace_ = {};
}

void SystemScheduler::SetSchedulingMode(SystemSchedulingMode mode) noexcept {
    if (schedulingMode_ == mode) {
        return;
    }

    schedulingMode_ = mode;
    graphDirty_ = true;
}

SystemSchedulingMode SystemScheduler::SchedulingMode() const noexcept {
    return schedulingMode_;
}

std::vector<std::string> SystemScheduler::ExecutionOrderSnapshot() const {
    const std::vector<std::size_t> order = graphDirty_ ? BuildExecutionOrder(BuildDependencyGraph()) : executionOrder_;
    std::vector<std::string> names;
    names.reserve(order.size());
    for (std::size_t systemIndex : order) {
        names.emplace_back(systems_[systemIndex].system->Name());
    }
    return names;
}

void SystemScheduler::SetDebugTraceEnabled(bool enabled) noexcept {
    debugTraceEnabled_ = enabled;
}

bool SystemScheduler::DebugTraceEnabled() const noexcept {
    return debugTraceEnabled_;
}

const SystemSchedulerTrace& SystemScheduler::LastDebugTrace() const noexcept {
    return lastTrace_;
}

void SystemScheduler::SetProfilerEnabled(bool enabled) noexcept {
    profilerEnabled_ = enabled;
}

bool SystemScheduler::ProfilerEnabled() const noexcept {
    return profilerEnabled_;
}

const SystemSchedulerTrace& SystemScheduler::LastProfilerTrace() const noexcept {
    return lastTrace_;
}

std::vector<std::vector<std::size_t>> SystemScheduler::BuildDependencyGraph() const {
    std::vector<std::vector<std::size_t>> graph(systems_.size());
    struct ComponentAccessHistory {
        std::vector<std::size_t> readers;
        std::vector<std::size_t> writers;
    };
    std::unordered_map<ComponentId, ComponentAccessHistory> accessHistory;
    std::vector<std::size_t> seen(systems_.size(), systems_.size());
    std::vector<std::size_t> conflicts;
    std::vector<std::size_t> syncPointPrefix(systems_.size() + 1U, 0U);
    for (std::size_t index = 0; index < systems_.size(); ++index) {
        syncPointPrefix[index + 1U] = syncPointPrefix[index] + (systems_[index].access.HasSyncPoint() ? 1U : 0U);
    }

    for (std::size_t second = 0; second < systems_.size(); ++second) {
        conflicts.clear();
        const SystemAccess& access = systems_[second].access;
        const auto appendConflicts = [&conflicts, &seen, second](const std::vector<std::size_t>& systems) {
            for (std::size_t first : systems) {
                if (seen[first] != second) {
                    seen[first] = second;
                    conflicts.push_back(first);
                }
            }
        };
        for (ComponentId componentId : access.WriteComponents()) {
            const auto found = accessHistory.find(componentId);
            if (found != accessHistory.end()) {
                appendConflicts(found->second.readers);
                appendConflicts(found->second.writers);
            }
        }
        for (ComponentId componentId : access.ReadComponents()) {
            const auto found = accessHistory.find(componentId);
            if (found != accessHistory.end()) {
                appendConflicts(found->second.writers);
            }
        }
        for (std::size_t first : conflicts) {
            if (schedulingMode_ == SystemSchedulingMode::Deterministic &&
                syncPointPrefix[second + 1U] == syncPointPrefix[first] &&
                IsBeforeInSchedulingOrder(second, first)) {
                graph[second].push_back(first);
            } else {
                graph[first].push_back(second);
            }
        }
        for (ComponentId componentId : access.WriteComponents()) {
            accessHistory[componentId].writers.push_back(second);
        }
        for (ComponentId componentId : access.ReadComponents()) {
            accessHistory[componentId].readers.push_back(second);
        }
    }

    // A barrier only needs edges to the adjacent registration segment. Earlier
    // and later segments remain ordered transitively through preceding barriers.
    std::size_t precedingSyncPoint = systems_.size();
    std::size_t segmentStart = 0U;
    for (std::size_t index = 0; index < systems_.size(); ++index) {
        if (systems_[index].access.HasSyncPoint()) {
            for (std::size_t before = segmentStart; before < index; ++before) {
                AddEdge(graph, before, index);
            }
            if (precedingSyncPoint != systems_.size()) {
                AddEdge(graph, precedingSyncPoint, index);
            }
            precedingSyncPoint = index;
            segmentStart = index + 1U;
        } else if (precedingSyncPoint != systems_.size()) {
            AddEdge(graph, precedingSyncPoint, index);
        }
    }

    std::unordered_map<std::string, std::size_t> systemNames;
    std::unordered_set<std::string> duplicateNames;
    for (std::size_t index = 0; index < systems_.size(); ++index) {
        const std::string name{ systems_[index].system->Name() };
        const auto insertResult = systemNames.emplace(name, index);
        if (!insertResult.second) {
            duplicateNames.insert(name);
        }
    }

    const auto resolveSystemName = [&systemNames, &duplicateNames](std::string_view name) -> std::size_t {
        const std::string key{ name };
        if (duplicateNames.find(key) != duplicateNames.end()) {
            throw std::invalid_argument("ECS system ordering references a non-unique system name");
        }
        const auto found = systemNames.find(key);
        if (found == systemNames.end()) {
            throw std::invalid_argument("ECS system ordering references an unknown system");
        }
        return found->second;
    };

    for (std::size_t index = 0; index < systems_.size(); ++index) {
        const SystemAccess& access = systems_[index].access;
        for (const std::string& dependency : access.RunAfter()) {
            AddEdge(graph, resolveSystemName(dependency), index);
        }
        for (const std::string& dependent : access.RunBefore()) {
            AddEdge(graph, index, resolveSystemName(dependent));
        }
    }

    for (std::vector<std::size_t>& edges : graph) {
        if (!std::is_sorted(edges.begin(), edges.end())) {
            std::sort(edges.begin(), edges.end());
        }
        edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    }

    return graph;
}

std::vector<std::vector<std::size_t>> SystemScheduler::BuildReverseDependencyGraph(const std::vector<std::vector<std::size_t>>& graph) {
    std::vector<std::vector<std::size_t>> reverseGraph(graph.size());
    for (std::size_t source = 0; source < graph.size(); ++source) {
        for (std::size_t target : graph[source]) {
            reverseGraph[target].push_back(source);
        }
    }
    return reverseGraph;
}

std::vector<std::size_t> SystemScheduler::BuildExecutionOrder(const std::vector<std::vector<std::size_t>>& graph) const {
    std::vector<std::size_t> indegree(graph.size(), 0);
    for (const auto& edges : graph) {
        for (std::size_t target : edges) {
            ++indegree[target];
        }
    }

    std::vector<std::size_t> order;
    order.reserve(graph.size());
    const auto lowerPriority = [this](std::size_t left, std::size_t right) {
        return IsBeforeInSchedulingOrder(right, left);
    };
    std::priority_queue<std::size_t, std::vector<std::size_t>, decltype(lowerPriority)> ready(lowerPriority);
    for (std::size_t index = 0; index < graph.size(); ++index) {
        if (indegree[index] == 0U) {
            ready.push(index);
        }
    }

    while (!ready.empty()) {
        const std::size_t next = ready.top();
        ready.pop();
        order.push_back(next);
        for (std::size_t target : graph[next]) {
            if (--indegree[target] == 0U) {
                ready.push(target);
            }
        }
    }

    if (order.size() != graph.size()) {
        throw std::runtime_error("ECS system dependency graph contains a cycle");
    }

    return order;
}

std::vector<SystemScheduler::ExecutionStage> SystemScheduler::BuildExecutionStages(
    const std::vector<std::size_t>& order,
    const std::vector<std::vector<std::size_t>>& graph) const {
    std::vector<ExecutionStage> stages;

    std::vector<std::size_t> indegree(graph.size(), 0);
    for (const auto& edges : graph) {
        for (std::size_t target : edges) {
            ++indegree[target];
        }
    }

    std::vector<std::size_t> orderRank(graph.size());
    for (std::size_t rank = 0; rank < order.size(); ++rank) {
        orderRank[order[rank]] = rank;
    }
    const auto lowerPriority = [&orderRank](std::size_t left, std::size_t right) {
        return orderRank[left] > orderRank[right];
    };
    std::priority_queue<std::size_t, std::vector<std::size_t>, decltype(lowerPriority)> ready(lowerPriority);
    for (std::size_t index = 0; index < graph.size(); ++index) {
        if (indegree[index] == 0U) {
            ready.push(index);
        }
    }

    std::size_t emittedCount = 0;
    while (emittedCount < graph.size()) {
        ExecutionStage stage;
        std::size_t available = ready.size();
        while (available-- > 0U) {
            const std::size_t systemIndex = ready.top();
            ready.pop();
            stage.systems.push_back(systemIndex);
            if (systems_[systemIndex].access.HasSyncPoint()) {
                break;
            }
        }

        if (stage.systems.empty()) {
            throw std::runtime_error("ECS system dependency graph contains a cycle");
        }

        for (std::size_t systemIndex : stage.systems) {
            ++emittedCount;
            for (std::size_t target : graph[systemIndex]) {
                if (--indegree[target] == 0U) {
                    ready.push(target);
                }
            }
        }
        stages.push_back(std::move(stage));
    }

    return stages;
}

void SystemScheduler::RebuildExecutionOrder() {
    const std::vector<std::vector<std::size_t>> graph = BuildDependencyGraph();
    std::vector<std::size_t> order = BuildExecutionOrder(graph);
    std::vector<ExecutionStage> stages = BuildExecutionStages(order, graph);
    std::vector<std::vector<std::size_t>> reverseGraph = BuildReverseDependencyGraph(graph);
    executionOrder_ = std::move(order);
    executionStages_ = std::move(stages);
    reverseDependencyGraph_ = std::move(reverseGraph);
    graphDirty_ = false;
}

void SystemScheduler::BeginDebugTrace() {
    BeginProfilerTrace();
}

void SystemScheduler::EndDebugTrace(std::uint64_t frameDurationNanoseconds) {
    EndProfilerTrace(frameDurationNanoseconds);
}

void SystemScheduler::BeginProfilerTrace() {
    lastTrace_ = SystemSchedulerTrace{
        .frameIndex = traceFrameIndex_,
        .frameCounters = SystemSchedulerFrameCounters{
            .frameIndex = traceFrameIndex_,
            .systemCount = systems_.size(),
            .stageCount = executionStages_.size(),
        },
    };
    lastTrace_.events.reserve(systems_.size());
    lastTrace_.stageCounters.reserve(executionStages_.size());
    for (std::size_t stageIndex = 0; stageIndex < executionStages_.size(); ++stageIndex) {
        lastTrace_.stageCounters.push_back(SystemSchedulerStageCounters{
            .stageIndex = stageIndex,
            .systemCount = executionStages_[stageIndex].systems.size(),
        });
    }
    lastTrace_.systemCounters.reserve(systems_.size());
    traceCounterIndexBySystem_.assign(systems_.size(), std::numeric_limits<std::size_t>::max());
    lastTrace_.workers.push_back(SystemSchedulerWorkerTrace{
        .workerIndex = 0,
    });
}

void SystemScheduler::EndProfilerTrace(std::uint64_t frameDurationNanoseconds) {
    lastTrace_.frameDurationNanoseconds = frameDurationNanoseconds;
    lastTrace_.frameCounters.frameDurationNanoseconds = frameDurationNanoseconds;
    lastTrace_.frameCounters.workerCount = lastTrace_.workers.size();
    if (workerPool_ != nullptr) {
        const WorkerPoolDispatchTelemetry dispatchTelemetry = workerPool_->DispatchTelemetry();
        lastTrace_.frameCounters.lastWorkerDispatchMode = std::string{ WorkerDispatchModeTraceName(dispatchTelemetry.lastMode) };
        lastTrace_.frameCounters.workerDispatchCount = dispatchTelemetry.dispatchCount;
        lastTrace_.frameCounters.workerStaticStridedDispatchCount = dispatchTelemetry.staticStridedDispatchCount;
        lastTrace_.frameCounters.workerQueuedDispatchCount = dispatchTelemetry.queuedDispatchCount;
        lastTrace_.frameCounters.lastWorkerDispatchWorkItemCount = dispatchTelemetry.lastWorkItemCount;
        lastTrace_.frameCounters.lastWorkerDispatchActiveWorkerCount = dispatchTelemetry.lastActiveWorkerCount;
        lastTrace_.frameCounters.lastWorkerDispatchConfiguredWorkerCount = dispatchTelemetry.lastConfiguredWorkerCount;
        lastTrace_.frameCounters.lastWorkerStealCount = dispatchTelemetry.lastStealCount;
        lastTrace_.frameCounters.workerStealCount = dispatchTelemetry.totalStealCount;
        lastTrace_.frameCounters.lastWorkerDispatchScheduleNanoseconds = dispatchTelemetry.lastDispatchScheduleNanoseconds;
        lastTrace_.frameCounters.workerDispatchScheduleNanoseconds = dispatchTelemetry.totalDispatchScheduleNanoseconds;
        lastTrace_.frameCounters.averageWorkerDispatchScheduleNanoseconds = dispatchTelemetry.averageDispatchScheduleNanoseconds;
        lastTrace_.frameCounters.lastWorkerDispatchWallNanoseconds = dispatchTelemetry.lastDispatchWallNanoseconds;
        lastTrace_.frameCounters.workerDispatchWallNanoseconds = dispatchTelemetry.totalDispatchWallNanoseconds;
        lastTrace_.frameCounters.averageWorkerDispatchWallNanoseconds = dispatchTelemetry.averageDispatchWallNanoseconds;
        lastTrace_.frameCounters.lastWorkerActiveNanoseconds = dispatchTelemetry.lastWorkerActiveNanoseconds;
        lastTrace_.frameCounters.workerActiveNanoseconds = dispatchTelemetry.totalWorkerActiveNanoseconds;
        lastTrace_.frameCounters.averageWorkerActiveNanoseconds = dispatchTelemetry.averageWorkerActiveNanoseconds;
        lastTrace_.frameCounters.lastWorkerCapacityNanoseconds = dispatchTelemetry.lastWorkerCapacityNanoseconds;
        lastTrace_.frameCounters.workerCapacityNanoseconds = dispatchTelemetry.totalWorkerCapacityNanoseconds;
        lastTrace_.frameCounters.lastWorkerUtilizationPercent = dispatchTelemetry.lastWorkerUtilizationPercent;
        lastTrace_.frameCounters.averageWorkerUtilizationPercent = dispatchTelemetry.averageWorkerUtilizationPercent;
    }
    for (SystemSchedulerWorkerTrace& worker : lastTrace_.workers) {
        const std::uint64_t busyTime = worker.busyTimeNanoseconds;
        worker.idleTimeNanoseconds = frameDurationNanoseconds > busyTime ? frameDurationNanoseconds - busyTime : 0;
        worker.utilizationPermille = frameDurationNanoseconds > 0U
            ? static_cast<std::uint32_t>(std::min<std::uint64_t>((busyTime * 1000U) / frameDurationNanoseconds, 1000U))
            : 0U;
    }
    ++traceFrameIndex_;
}

void SystemScheduler::TraceSystemExecution(
    std::size_t systemIndex,
    std::size_t stageIndex,
    std::size_t workerIndex,
    std::uint64_t jobsCount,
    std::uint64_t startTimeNanoseconds,
    std::uint64_t endTimeNanoseconds,
    SystemProfilerCounters profilerCounters,
    std::span<const std::uint64_t> systemEndTimes,
    const std::vector<std::vector<std::size_t>>& reverseGraph) {
    std::uint64_t dependencyReadyTime = 0;
    std::vector<std::string> blockedDependencies;
    bool hasBlockedDependencies = false;
    if (systemIndex < reverseGraph.size()) {
        blockedDependencies.reserve(reverseGraph[systemIndex].size());
        for (std::size_t dependencyIndex : reverseGraph[systemIndex]) {
            hasBlockedDependencies = true;
            if (dependencyIndex < systemEndTimes.size()) {
                dependencyReadyTime = std::max(dependencyReadyTime, systemEndTimes[dependencyIndex]);
            }
            blockedDependencies.emplace_back(systems_[dependencyIndex].system->Name());
        }
    }

    const std::uint64_t duration = endTimeNanoseconds >= startTimeNanoseconds ? endTimeNanoseconds - startTimeNanoseconds : 0;
    while (workerIndex >= lastTrace_.workers.size()) {
        lastTrace_.workers.push_back(SystemSchedulerWorkerTrace{
            .workerIndex = lastTrace_.workers.size(),
        });
    }
    lastTrace_.workers[workerIndex].busyTimeNanoseconds += duration;

    SystemSchedulerTraceEvent event{
        .systemName = std::string{ systems_[systemIndex].system->Name() },
        .executionPath = std::string{ systems_[systemIndex].system->ExecutionPathName() },
        .systemIndex = systemIndex,
        .stageIndex = stageIndex,
        .workerIndex = workerIndex,
        .jobsCount = jobsCount,
        .chunkJobsCount = profilerCounters.chunkJobsCount,
        .startTimeNanoseconds = startTimeNanoseconds,
        .endTimeNanoseconds = endTimeNanoseconds,
        .durationNanoseconds = duration,
        .waitTimeNanoseconds = hasBlockedDependencies && startTimeNanoseconds > dependencyReadyTime ? startTimeNanoseconds - dependencyReadyTime : 0,
        .entitiesProcessed = profilerCounters.entitiesProcessed,
        .bytesTouched = profilerCounters.bytesTouched,
        .waitReason = hasBlockedDependencies ? std::string{ "dependencies" } : std::string{},
        .blockedDependencies = std::move(blockedDependencies),
    };
    AddSystemCounters(event);
    AddStageCounters(event);
    lastTrace_.events.push_back(std::move(event));
}

void SystemScheduler::AddSystemCounters(const SystemSchedulerTraceEvent& event) {
    std::size_t& counterIndex = traceCounterIndexBySystem_[event.systemIndex];
    if (counterIndex == std::numeric_limits<std::size_t>::max()) {
        counterIndex = lastTrace_.systemCounters.size();
        lastTrace_.systemCounters.emplace_back(SystemSchedulerSystemCounters{
            .systemName = event.systemName,
            .executionPath = event.executionPath,
            .systemIndex = event.systemIndex,
        });
    }
    SystemSchedulerSystemCounters& counters = lastTrace_.systemCounters[counterIndex];

    counters.cpuTimeNanoseconds += event.durationNanoseconds;
    counters.jobsCount += event.jobsCount;
    counters.chunkJobsCount += event.chunkJobsCount;
    counters.entitiesProcessed += event.entitiesProcessed;
    counters.bytesTouched += event.bytesTouched;

    lastTrace_.frameCounters.cpuTimeNanoseconds += event.durationNanoseconds;
    lastTrace_.frameCounters.jobsCount += event.jobsCount;
    lastTrace_.frameCounters.chunkJobsCount += event.chunkJobsCount;
    lastTrace_.frameCounters.entitiesProcessed += event.entitiesProcessed;
    lastTrace_.frameCounters.bytesTouched += event.bytesTouched;
}

void SystemScheduler::AddStageCounters(const SystemSchedulerTraceEvent& event) {
    SystemSchedulerStageCounters& counters = lastTrace_.stageCounters[event.stageIndex];
    counters.cpuTimeNanoseconds += event.durationNanoseconds;
    counters.jobsCount += event.jobsCount;
    counters.chunkJobsCount += event.chunkJobsCount;
    counters.waitTimeNanoseconds += event.waitTimeNanoseconds;
    counters.workerBusyTimeNanoseconds += event.durationNanoseconds;
}

bool SystemScheduler::ShouldRunStageInParallel(const ExecutionStage& stage) const noexcept {
    return parallelExecutionEnabled_ &&
        parallelismMode_ == SystemSchedulerParallelismMode::SystemStages &&
        schedulingMode_ != SystemSchedulingMode::Deterministic &&
        !workerPoolConfig_.singleThreaded &&
        stage.systems.size() > 1U;
}

bool SystemScheduler::InstrumentationEnabled() const noexcept {
    return debugTraceEnabled_ || profilerEnabled_;
}

WorkerPool& SystemScheduler::RuntimeWorkerPool() {
    if (workerPool_ == nullptr) {
        workerPool_ = std::make_unique<WorkerPool>(workerPoolConfig_);
    }
    return *workerPool_;
}

bool SystemScheduler::IsBeforeInSchedulingOrder(std::size_t left, std::size_t right) const noexcept {
    if (schedulingMode_ != SystemSchedulingMode::Deterministic) {
        return left < right;
    }

    const std::string_view leftName = systems_[left].system->Name();
    const std::string_view rightName = systems_[right].system->Name();
    if (leftName == rightName) {
        return left < right;
    }
    return leftName < rightName;
}

void SystemScheduler::AddEdge(std::vector<std::vector<std::size_t>>& graph, std::size_t from, std::size_t to) {
    if (from == to) {
        throw std::invalid_argument("ECS system dependency graph cannot contain a self edge");
    }

    graph[from].push_back(to);
}

} // namespace kb::ecs
