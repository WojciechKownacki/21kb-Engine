#include "engine/ecs/SystemScheduler.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
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

    std::vector<std::vector<std::size_t>> reverseGraph;
    std::vector<std::uint64_t> systemEndTimes;
    std::chrono::steady_clock::time_point frameStart;
    const bool instrumentationEnabled = InstrumentationEnabled();
    if (instrumentationEnabled) {
        BeginProfilerTrace();
        reverseGraph = BuildReverseDependencyGraph();
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
                        reverseGraph);
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
                    TraceSystemExecution(systemIndex, stageIndex, 0, 1, startTimeNanoseconds, endTimeNanoseconds, systems_[systemIndex].system->ProfilerCounters(), systemEndTimes, reverseGraph);
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
    const std::vector<std::size_t> order = BuildExecutionOrder();
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

    for (std::size_t first = 0; first < systems_.size(); ++first) {
        for (std::size_t second = first + 1; second < systems_.size(); ++second) {
            if (AccessConflicts(systems_[first].access, systems_[second].access)) {
                if (schedulingMode_ == SystemSchedulingMode::Deterministic && !IsSeparatedBySyncPoint(first, second) && IsBeforeInSchedulingOrder(second, first)) {
                    AddEdge(graph, second, first);
                } else {
                    AddEdge(graph, first, second);
                }
            }
        }
    }

    for (std::size_t index = 0; index < systems_.size(); ++index) {
        if (systems_[index].access.HasSyncPoint()) {
            AddSyncPointEdges(graph, index);
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

    return graph;
}

std::vector<std::vector<std::size_t>> SystemScheduler::BuildReverseDependencyGraph() const {
    const std::vector<std::vector<std::size_t>> graph = BuildDependencyGraph();
    std::vector<std::vector<std::size_t>> reverseGraph(graph.size());
    for (std::size_t source = 0; source < graph.size(); ++source) {
        for (std::size_t target : graph[source]) {
            reverseGraph[target].push_back(source);
        }
    }
    return reverseGraph;
}

std::vector<std::size_t> SystemScheduler::BuildExecutionOrder() const {
    const std::vector<std::vector<std::size_t>> graph = BuildDependencyGraph();
    std::vector<std::size_t> indegree(graph.size(), 0);
    for (const auto& edges : graph) {
        for (std::size_t target : edges) {
            ++indegree[target];
        }
    }

    std::vector<std::size_t> order;
    order.reserve(graph.size());
    std::vector<bool> emitted(graph.size(), false);

    while (order.size() < graph.size()) {
        std::size_t next = graph.size();
        for (std::size_t index = 0; index < graph.size(); ++index) {
            if (!emitted[index] && indegree[index] == 0) {
                if (next == graph.size() || IsBeforeInSchedulingOrder(index, next)) {
                    next = index;
                }
            }
        }

        if (next == graph.size()) {
            throw std::runtime_error("ECS system dependency graph contains a cycle");
        }

        emitted[next] = true;
        order.push_back(next);
        for (std::size_t target : graph[next]) {
            --indegree[target];
        }
    }

    return order;
}

std::vector<SystemScheduler::ExecutionStage> SystemScheduler::BuildExecutionStages(const std::vector<std::size_t>& order) const {
    std::vector<ExecutionStage> stages;

    const std::vector<std::vector<std::size_t>> graph = BuildDependencyGraph();
    std::vector<std::size_t> indegree(graph.size(), 0);
    for (const auto& edges : graph) {
        for (std::size_t target : edges) {
            ++indegree[target];
        }
    }

    std::vector<bool> emitted(graph.size(), false);
    std::size_t emittedCount = 0;
    while (emittedCount < graph.size()) {
        ExecutionStage stage;
        for (std::size_t systemIndex : order) {
            if (!emitted[systemIndex] && indegree[systemIndex] == 0) {
                stage.systems.push_back(systemIndex);
                if (systems_[systemIndex].access.HasSyncPoint()) {
                    break;
                }
            }
        }

        if (stage.systems.empty()) {
            throw std::runtime_error("ECS system dependency graph contains a cycle");
        }

        for (std::size_t systemIndex : stage.systems) {
            emitted[systemIndex] = true;
            ++emittedCount;
            for (std::size_t target : graph[systemIndex]) {
                --indegree[target];
            }
        }
        stages.push_back(std::move(stage));
    }

    return stages;
}

void SystemScheduler::RebuildExecutionOrder() {
    executionOrder_ = BuildExecutionOrder();
    executionStages_ = BuildExecutionStages(executionOrder_);
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
    lastTrace_.workers.push_back(SystemSchedulerWorkerTrace{
        .workerIndex = 0,
    });
}

void SystemScheduler::EndProfilerTrace(std::uint64_t frameDurationNanoseconds) {
    lastTrace_.frameDurationNanoseconds = frameDurationNanoseconds;
    lastTrace_.frameCounters.frameDurationNanoseconds = frameDurationNanoseconds;
    lastTrace_.frameCounters.workerCount = lastTrace_.workers.size();
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
    SystemSchedulerSystemCounters* counters = nullptr;
    for (SystemSchedulerSystemCounters& existing : lastTrace_.systemCounters) {
        if (existing.systemIndex == event.systemIndex) {
            counters = &existing;
            break;
        }
    }

    if (counters == nullptr) {
        counters = &lastTrace_.systemCounters.emplace_back(SystemSchedulerSystemCounters{
            .systemName = event.systemName,
            .executionPath = event.executionPath,
            .systemIndex = event.systemIndex,
        });
    }

    counters->cpuTimeNanoseconds += event.durationNanoseconds;
    counters->jobsCount += event.jobsCount;
    counters->chunkJobsCount += event.chunkJobsCount;
    counters->entitiesProcessed += event.entitiesProcessed;
    counters->bytesTouched += event.bytesTouched;

    lastTrace_.frameCounters.cpuTimeNanoseconds += event.durationNanoseconds;
    lastTrace_.frameCounters.jobsCount += event.jobsCount;
    lastTrace_.frameCounters.chunkJobsCount += event.chunkJobsCount;
    lastTrace_.frameCounters.entitiesProcessed += event.entitiesProcessed;
    lastTrace_.frameCounters.bytesTouched += event.bytesTouched;
}

void SystemScheduler::AddStageCounters(const SystemSchedulerTraceEvent& event) {
    SystemSchedulerStageCounters* counters = nullptr;
    for (SystemSchedulerStageCounters& existing : lastTrace_.stageCounters) {
        if (existing.stageIndex == event.stageIndex) {
            counters = &existing;
            break;
        }
    }

    if (counters == nullptr) {
        counters = &lastTrace_.stageCounters.emplace_back(SystemSchedulerStageCounters{
            .stageIndex = event.stageIndex,
        });
    }

    counters->cpuTimeNanoseconds += event.durationNanoseconds;
    counters->jobsCount += event.jobsCount;
    counters->chunkJobsCount += event.chunkJobsCount;
    counters->waitTimeNanoseconds += event.waitTimeNanoseconds;
    counters->workerBusyTimeNanoseconds += event.durationNanoseconds;
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

bool SystemScheduler::IsSeparatedBySyncPoint(std::size_t left, std::size_t right) const noexcept {
    if (left > right || right >= systems_.size()) {
        return false;
    }

    for (std::size_t index = left; index <= right; ++index) {
        if (systems_[index].access.HasSyncPoint()) {
            return true;
        }
    }
    return false;
}

bool SystemScheduler::HasComponent(const std::vector<ComponentId>& components, ComponentId componentId) noexcept {
    return std::binary_search(components.begin(), components.end(), componentId);
}

bool SystemScheduler::AccessConflicts(const SystemAccess& first, const SystemAccess& second) noexcept {
    for (ComponentId componentId : first.WriteComponents()) {
        if (HasComponent(second.ReadComponents(), componentId) || HasComponent(second.WriteComponents(), componentId)) {
            return true;
        }
    }
    for (ComponentId componentId : first.ReadComponents()) {
        if (HasComponent(second.WriteComponents(), componentId)) {
            return true;
        }
    }
    return false;
}

void SystemScheduler::AddSyncPointEdges(std::vector<std::vector<std::size_t>>& graph, std::size_t syncPointIndex) {
    for (std::size_t index = 0; index < syncPointIndex; ++index) {
        AddEdge(graph, index, syncPointIndex);
    }
    for (std::size_t index = syncPointIndex + 1; index < graph.size(); ++index) {
        AddEdge(graph, syncPointIndex, index);
    }
}

void SystemScheduler::AddEdge(std::vector<std::vector<std::size_t>>& graph, std::size_t from, std::size_t to) {
    if (from == to) {
        throw std::invalid_argument("ECS system dependency graph cannot contain a self edge");
    }

    std::vector<std::size_t>& edges = graph[from];
    if (std::find(edges.begin(), edges.end(), to) == edges.end()) {
        edges.push_back(to);
    }
}

} // namespace kb::ecs
