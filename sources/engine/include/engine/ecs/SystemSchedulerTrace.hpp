#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace kb::ecs {

struct SystemSchedulerTraceEvent {
    std::string systemName;
    std::size_t systemIndex = 0;
    std::size_t stageIndex = 0;
    std::size_t workerIndex = 0;
    std::uint64_t startTimeNanoseconds = 0;
    std::uint64_t endTimeNanoseconds = 0;
    std::uint64_t durationNanoseconds = 0;
    std::uint64_t waitTimeNanoseconds = 0;
    std::vector<std::string> blockedDependencies;
};

struct SystemSchedulerWorkerTrace {
    std::size_t workerIndex = 0;
    std::uint64_t busyTimeNanoseconds = 0;
    std::uint64_t idleTimeNanoseconds = 0;
};

struct SystemSchedulerTrace {
    std::uint64_t frameIndex = 0;
    std::uint64_t frameDurationNanoseconds = 0;
    std::vector<SystemSchedulerTraceEvent> events;
    std::vector<SystemSchedulerWorkerTrace> workers;
};

} // namespace kb::ecs
