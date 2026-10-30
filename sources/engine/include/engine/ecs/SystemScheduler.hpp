#pragma once

#include "engine/ecs/RuntimeAccessValidator.hpp"
#include "engine/ecs/System.hpp"
#include "engine/ecs/SystemSchedulerTrace.hpp"
#include "engine/ecs/WorkerPool.hpp"
#include "engine/ecs/World.hpp"

#include <cstddef>
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace kb::ecs {

enum class SystemSchedulingMode {
    Automatic,
    Deterministic,
};

enum class SystemSchedulerParallelismMode {
    SystemStages,
    QueryChunks,
    SystemsAndChunks,
};

struct SystemSchedulerConfig {
    SystemSchedulingMode mode = SystemSchedulingMode::Automatic;
    bool debugTraceEnabled = false;
    bool profilerEnabled = false;
    bool parallelExecutionEnabled = true;
    SystemSchedulerParallelismMode parallelismMode = SystemSchedulerParallelismMode::SystemStages;
    bool runtimeAccessValidationEnabled = true;
    WorkerPoolConfig workerPool{};
};

struct SystemSchedulerPlanningStats {
    std::uint64_t attempts = 0U;
    std::uint64_t successfulRebuilds = 0U;
    std::uint64_t lastWallNanoseconds = 0U;
    std::size_t systemCount = 0U;
    std::size_t edgeCount = 0U;
    std::size_t stageCount = 0U;
    bool lastSucceeded = false;
};

class SystemScheduler {
public:
    SystemScheduler() = default;
    explicit SystemScheduler(SystemSchedulerConfig config) noexcept;
    ~SystemScheduler();

    SystemScheduler(const SystemScheduler&) = delete;
    SystemScheduler& operator=(const SystemScheduler&) = delete;
    SystemScheduler(SystemScheduler&&) noexcept = delete;
    SystemScheduler& operator=(SystemScheduler&&) noexcept = delete;

    void Add(std::unique_ptr<System> system, World& world);
    // Owner-thread operation between updates. The pointer identifies an owned
    // system, without relying on its display name. OnDestroy runs once after
    // detachment; a throwing callback cannot leave the removed system scheduled.
    [[nodiscard]] bool Remove(const System* system, World& world);
    void Update(World& world, float deltaSeconds);
    void Shutdown(World& world);

    void SetSchedulingMode(SystemSchedulingMode mode) noexcept;
    [[nodiscard]] SystemSchedulingMode SchedulingMode() const noexcept;
    // Configure between updates on the owner thread.
    void SetParallelismMode(SystemSchedulerParallelismMode mode) noexcept;
    [[nodiscard]] SystemSchedulerParallelismMode ParallelismMode() const noexcept;
    [[nodiscard]] std::vector<std::string> ExecutionOrderSnapshot() const;
    void SetDebugTraceEnabled(bool enabled) noexcept;
    [[nodiscard]] bool DebugTraceEnabled() const noexcept;
    [[nodiscard]] const SystemSchedulerTrace& LastDebugTrace() const noexcept;
    void SetProfilerEnabled(bool enabled) noexcept;
    [[nodiscard]] bool ProfilerEnabled() const noexcept;
    [[nodiscard]] const SystemSchedulerTrace& LastProfilerTrace() const noexcept;
    // Update's cached execution-plan rebuilds only; excludes system execution.
    // ExecutionOrderSnapshot is an unmeasured inspection. Cached frames leave
    // these measurements unchanged. Shutdown resets the counters.
    [[nodiscard]] const SystemSchedulerPlanningStats& PlanningStats() const noexcept;

private:
    struct ScheduledSystem {
        std::unique_ptr<System> system;
        SystemAccess access;
    };

    struct ExecutionStage {
        std::vector<std::size_t> systems;
    };

    [[nodiscard]] std::vector<std::vector<std::size_t>> BuildDependencyGraph() const;
    [[nodiscard]] static std::vector<std::vector<std::size_t>> BuildReverseDependencyGraph(const std::vector<std::vector<std::size_t>>& graph);
    [[nodiscard]] std::vector<std::size_t> BuildExecutionOrder(const std::vector<std::vector<std::size_t>>& graph) const;
    [[nodiscard]] std::vector<ExecutionStage> BuildExecutionStages(
        const std::vector<std::size_t>& order,
        const std::vector<std::vector<std::size_t>>& graph) const;
    void RebuildExecutionOrder();
    void UpdateGraph(World& world, float deltaSeconds);
    void BeginDebugTrace();
    void EndDebugTrace(std::uint64_t frameDurationNanoseconds);
    void BeginProfilerTrace();
    void EndProfilerTrace(std::uint64_t frameDurationNanoseconds);
    void TraceSystemExecution(
        std::size_t systemIndex,
        std::size_t stageIndex,
        std::size_t workerIndex,
        std::uint64_t jobsCount,
        std::uint64_t startTimeNanoseconds,
        std::uint64_t endTimeNanoseconds,
        SystemProfilerCounters profilerCounters,
        std::span<const std::uint64_t> systemEndTimes,
        const std::vector<std::vector<std::size_t>>& reverseGraph);
    void AddSystemCounters(const SystemSchedulerTraceEvent& event);
    void AddStageCounters(const SystemSchedulerTraceEvent& event);
    [[nodiscard]] bool ShouldRunStageInParallel(const ExecutionStage& stage) const noexcept;
    [[nodiscard]] bool InstrumentationEnabled() const noexcept;
    [[nodiscard]] WorkerPool& RuntimeWorkerPool();

    [[nodiscard]] bool IsBeforeInSchedulingOrder(std::size_t left, std::size_t right) const noexcept;
    static void AddEdge(std::vector<std::vector<std::size_t>>& graph, std::size_t from, std::size_t to);

    std::vector<ScheduledSystem> systems_;
    std::vector<std::size_t> executionOrder_;
    std::vector<ExecutionStage> executionStages_;
    std::vector<std::vector<std::size_t>> reverseDependencyGraph_;
    std::vector<std::size_t> traceCounterIndexBySystem_;
    SystemSchedulingMode schedulingMode_ = SystemSchedulingMode::Automatic;
    bool debugTraceEnabled_ = false;
    bool profilerEnabled_ = false;
    bool parallelExecutionEnabled_ = true;
    SystemSchedulerParallelismMode parallelismMode_ = SystemSchedulerParallelismMode::SystemStages;
    bool runtimeAccessValidationEnabled_ = true;
    WorkerPoolConfig workerPoolConfig_{};
    std::unique_ptr<WorkerPool> workerPool_;
    RuntimeAccessValidator accessValidator_;
    bool graphDirty_ = false;
    std::uint64_t traceFrameIndex_ = 0;
    SystemSchedulerTrace lastTrace_;
    SystemSchedulerPlanningStats planningStats_;
    std::atomic_bool operationActive_{false};
};

} // namespace kb::ecs
