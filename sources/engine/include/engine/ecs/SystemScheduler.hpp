#pragma once

#include "engine/ecs/System.hpp"
#include "engine/ecs/SystemSchedulerTrace.hpp"
#include "engine/ecs/WorkerPool.hpp"
#include "engine/ecs/World.hpp"

#include <cstddef>
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

struct SystemSchedulerConfig {
    SystemSchedulingMode mode = SystemSchedulingMode::Automatic;
    bool debugTraceEnabled = false;
    bool parallelExecutionEnabled = true;
    WorkerPoolConfig workerPool{};
};

class SystemScheduler {
public:
    SystemScheduler() = default;
    explicit SystemScheduler(SystemSchedulerConfig config) noexcept;
    ~SystemScheduler();

    SystemScheduler(const SystemScheduler&) = delete;
    SystemScheduler& operator=(const SystemScheduler&) = delete;
    SystemScheduler(SystemScheduler&&) noexcept = default;
    SystemScheduler& operator=(SystemScheduler&&) noexcept = default;

    void Add(std::unique_ptr<System> system, World& world);
    void Update(World& world, float deltaSeconds);
    void Shutdown(World& world);

    void SetSchedulingMode(SystemSchedulingMode mode) noexcept;
    [[nodiscard]] SystemSchedulingMode SchedulingMode() const noexcept;
    [[nodiscard]] std::vector<std::string> ExecutionOrderSnapshot() const;
    void SetDebugTraceEnabled(bool enabled) noexcept;
    [[nodiscard]] bool DebugTraceEnabled() const noexcept;
    [[nodiscard]] const SystemSchedulerTrace& LastDebugTrace() const noexcept;

private:
    struct ScheduledSystem {
        std::unique_ptr<System> system;
        SystemAccess access;
    };

    struct ExecutionStage {
        std::vector<std::size_t> systems;
    };

    [[nodiscard]] std::vector<std::vector<std::size_t>> BuildDependencyGraph() const;
    [[nodiscard]] std::vector<std::vector<std::size_t>> BuildReverseDependencyGraph() const;
    [[nodiscard]] std::vector<std::size_t> BuildExecutionOrder() const;
    [[nodiscard]] std::vector<ExecutionStage> BuildExecutionStages(const std::vector<std::size_t>& order) const;
    void RebuildExecutionOrder();
    void BeginDebugTrace();
    void EndDebugTrace(std::uint64_t frameDurationNanoseconds);
    void TraceSystemExecution(
        std::size_t systemIndex,
        std::size_t stageIndex,
        std::size_t workerIndex,
        std::uint64_t startTimeNanoseconds,
        std::uint64_t endTimeNanoseconds,
        std::span<const std::uint64_t> systemEndTimes,
        const std::vector<std::vector<std::size_t>>& reverseGraph);
    [[nodiscard]] bool ShouldRunStageInParallel(const ExecutionStage& stage) const noexcept;
    [[nodiscard]] WorkerPool& RuntimeWorkerPool();

    [[nodiscard]] bool IsBeforeInSchedulingOrder(std::size_t left, std::size_t right) const noexcept;
    [[nodiscard]] bool IsSeparatedBySyncPoint(std::size_t left, std::size_t right) const noexcept;
    [[nodiscard]] static bool HasComponent(const std::vector<ComponentId>& components, ComponentId componentId) noexcept;
    [[nodiscard]] static bool AccessConflicts(const SystemAccess& first, const SystemAccess& second) noexcept;
    static void AddSyncPointEdges(std::vector<std::vector<std::size_t>>& graph, std::size_t syncPointIndex);
    static void AddEdge(std::vector<std::vector<std::size_t>>& graph, std::size_t from, std::size_t to);

    std::vector<ScheduledSystem> systems_;
    std::vector<std::size_t> executionOrder_;
    std::vector<ExecutionStage> executionStages_;
    SystemSchedulingMode schedulingMode_ = SystemSchedulingMode::Automatic;
    bool debugTraceEnabled_ = false;
    bool parallelExecutionEnabled_ = true;
    WorkerPoolConfig workerPoolConfig_{};
    std::unique_ptr<WorkerPool> workerPool_;
    bool graphDirty_ = false;
    std::uint64_t traceFrameIndex_ = 0;
    SystemSchedulerTrace lastTrace_;
};

} // namespace kb::ecs
