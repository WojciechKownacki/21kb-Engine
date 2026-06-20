#pragma once

#include "engine/ecs/SystemSchedulerTrace.hpp"

#include <filesystem>

namespace kb::ecs {

void ExportSystemSchedulerTraceToJsonFile(const SystemSchedulerTrace& trace, const std::filesystem::path& path);

// Standalone Chrome Trace Event Format file (chrome://tracing / Perfetto ready):
// a top-level object with a "traceEvents" array of duration ("X") events keyed
// by worker thread.
void ExportSystemSchedulerTraceToChromeTraceFile(const SystemSchedulerTrace& trace, const std::filesystem::path& path);

// Flat CSV of per-system counters for spreadsheet analysis and regression diffs.
void ExportSystemSchedulerTraceToCsvFile(const SystemSchedulerTrace& trace, const std::filesystem::path& path);

} // namespace kb::ecs
