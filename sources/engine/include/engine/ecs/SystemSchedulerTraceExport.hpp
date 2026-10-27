#pragma once

#include "engine/ecs/SystemSchedulerTrace.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace kb::ecs {

void ExportSystemSchedulerTraceToJsonFile(const SystemSchedulerTrace& trace, const std::filesystem::path& path);

// Standalone Chrome Trace Event Format file (chrome://tracing / Perfetto ready):
// a top-level object with a "traceEvents" array of duration ("X") events keyed
// by worker thread.
void ExportSystemSchedulerTraceToChromeTraceFile(const SystemSchedulerTrace& trace, const std::filesystem::path& path);

// Flat CSV of per-system counters for spreadsheet analysis and regression diffs.
void ExportSystemSchedulerTraceToCsvFile(const SystemSchedulerTrace& trace, const std::filesystem::path& path);

// The chain of systems that decided how long the frame took: it ends with the system that finished last and, from
// there, follows for each system the dependency that finished last, back to a system that waited for nothing.
struct SystemSchedulerCriticalPath {
    // From the first system of the chain to the last.
    std::vector<std::string> systems;
    // From the start of the first system to the end of the last: wall time, including any wait between them.
    std::uint64_t wallNanoseconds = 0;
    // Time the systems of the chain spent running; wallNanoseconds minus this is time spent waiting.
    std::uint64_t busyNanoseconds = 0;
};

[[nodiscard]] SystemSchedulerCriticalPath ComputeSystemSchedulerCriticalPath(const SystemSchedulerTrace& trace);

// Sum of the durations of all systems. Systems on different workers overlap, so this is CPU time summed over workers
// and can exceed the frame's wall time; it is never a wall time.
[[nodiscard]] std::uint64_t SumSystemSchedulerCpuNanoseconds(const SystemSchedulerTrace& trace) noexcept;

} // namespace kb::ecs
