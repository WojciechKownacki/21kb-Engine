#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace kb::ecs {

struct SystemSchedulerTraceEvent {
    std::string systemName;
    std::string executionPath;
    std::size_t systemIndex = 0;
    std::size_t stageIndex = 0;
    std::size_t workerIndex = 0;
    std::uint64_t jobsCount = 0;
    std::uint64_t chunkJobsCount = 0;
    std::uint64_t startTimeNanoseconds = 0;
    std::uint64_t endTimeNanoseconds = 0;
    std::uint64_t durationNanoseconds = 0;
    std::uint64_t waitTimeNanoseconds = 0;
    std::uint64_t entitiesProcessed = 0;
    std::uint64_t bytesTouched = 0;
    std::string waitReason;
    std::vector<std::string> blockedDependencies;
};

struct SystemSchedulerWorkerTrace {
    std::size_t workerIndex = 0;
    std::uint64_t busyTimeNanoseconds = 0;
    std::uint64_t idleTimeNanoseconds = 0;
    std::uint32_t utilizationPermille = 0;
};

struct SystemSchedulerSystemCounters {
    std::string systemName;
    std::string executionPath;
    std::size_t systemIndex = 0;
    std::uint64_t cpuTimeNanoseconds = 0;
    std::uint64_t jobsCount = 0;
    std::uint64_t chunkJobsCount = 0;
    std::uint64_t entitiesProcessed = 0;
    std::uint64_t bytesTouched = 0;
};

struct SystemSchedulerStageCounters {
    std::size_t stageIndex = 0;
    std::size_t systemCount = 0;
    std::uint64_t cpuTimeNanoseconds = 0;
    std::uint64_t jobsCount = 0;
    std::uint64_t chunkJobsCount = 0;
    std::uint64_t waitTimeNanoseconds = 0;
    std::uint64_t workerBusyTimeNanoseconds = 0;
};

struct SystemSchedulerFrameCounters {
    std::uint64_t frameIndex = 0;
    std::uint64_t frameDurationNanoseconds = 0;
    std::uint64_t cpuTimeNanoseconds = 0;
    std::uint64_t jobsCount = 0;
    std::uint64_t chunkJobsCount = 0;
    std::uint64_t entitiesProcessed = 0;
    std::uint64_t bytesTouched = 0;
    std::size_t systemCount = 0;
    std::size_t stageCount = 0;
    std::size_t parallelStageCount = 0;
    std::size_t workerCount = 0;
};

struct SystemSchedulerTrace {
    std::uint64_t frameIndex = 0;
    std::uint64_t frameDurationNanoseconds = 0;
    SystemSchedulerFrameCounters frameCounters;
    std::vector<SystemSchedulerStageCounters> stageCounters;
    std::vector<SystemSchedulerSystemCounters> systemCounters;
    std::vector<SystemSchedulerTraceEvent> events;
    std::vector<SystemSchedulerWorkerTrace> workers;
};

} // namespace kb::ecs
