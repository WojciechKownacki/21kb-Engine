#include "engine/ecs/SystemSchedulerTraceExport.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <ostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace kb::ecs {
namespace {

void WriteJsonString(std::ostream& output, std::string_view value) {
    output << '"';
    for (const char character : value) {
        switch (character) {
        case '"':
            output << "\\\"";
            break;
        case '\\':
            output << "\\\\";
            break;
        case '\b':
            output << "\\b";
            break;
        case '\f':
            output << "\\f";
            break;
        case '\n':
            output << "\\n";
            break;
        case '\r':
            output << "\\r";
            break;
        case '\t':
            output << "\\t";
            break;
        default:
            if (static_cast<unsigned char>(character) < 0x20U) {
                output << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned>(static_cast<unsigned char>(character)) << std::dec << std::setfill(' ');
            } else {
                output << character;
            }
            break;
        }
    }
    output << '"';
}

[[nodiscard]] std::uint64_t NanosecondsToMicroseconds(std::uint64_t nanoseconds) noexcept {
    return nanoseconds / 1000U;
}

void WriteFrameCounters(std::ostream& output, const SystemSchedulerFrameCounters& counters) {
    output << "      \"frame_index\": " << counters.frameIndex << ",\n";
    output << "      \"frame_duration_ns\": " << counters.frameDurationNanoseconds << ",\n";
    output << "      \"cpu_time_ns\": " << counters.cpuTimeNanoseconds << ",\n";
    output << "      \"jobs_count\": " << counters.jobsCount << ",\n";
    output << "      \"chunk_jobs_count\": " << counters.chunkJobsCount << ",\n";
    output << "      \"entities_processed\": " << counters.entitiesProcessed << ",\n";
    output << "      \"bytes_touched\": " << counters.bytesTouched << ",\n";
    output << "      \"system_count\": " << counters.systemCount << ",\n";
    output << "      \"stage_count\": " << counters.stageCount << ",\n";
    output << "      \"parallel_stage_count\": " << counters.parallelStageCount << ",\n";
    output << "      \"worker_count\": " << counters.workerCount << ",\n";
    output << "      \"last_worker_dispatch_mode\": ";
    WriteJsonString(output, counters.lastWorkerDispatchMode);
    output << ",\n";
    output << "      \"worker_dispatch_count\": " << counters.workerDispatchCount << ",\n";
    output << "      \"worker_static_strided_dispatch_count\": " << counters.workerStaticStridedDispatchCount << ",\n";
    output << "      \"worker_queued_dispatch_count\": " << counters.workerQueuedDispatchCount << ",\n";
    output << "      \"last_worker_dispatch_work_item_count\": " << counters.lastWorkerDispatchWorkItemCount << ",\n";
    output << "      \"last_worker_dispatch_active_worker_count\": " << counters.lastWorkerDispatchActiveWorkerCount << ",\n";
    output << "      \"last_worker_dispatch_configured_worker_count\": " << counters.lastWorkerDispatchConfiguredWorkerCount << ",\n";
    output << "      \"last_worker_steal_count\": " << counters.lastWorkerStealCount << ",\n";
    output << "      \"worker_steal_count\": " << counters.workerStealCount << ",\n";
    output << "      \"last_worker_dispatch_schedule_ns\": " << counters.lastWorkerDispatchScheduleNanoseconds << ",\n";
    output << "      \"worker_dispatch_schedule_ns\": " << counters.workerDispatchScheduleNanoseconds << ",\n";
    output << "      \"average_worker_dispatch_schedule_ns\": " << counters.averageWorkerDispatchScheduleNanoseconds << ",\n";
    output << "      \"last_worker_dispatch_wall_ns\": " << counters.lastWorkerDispatchWallNanoseconds << ",\n";
    output << "      \"worker_dispatch_wall_ns\": " << counters.workerDispatchWallNanoseconds << ",\n";
    output << "      \"average_worker_dispatch_wall_ns\": " << counters.averageWorkerDispatchWallNanoseconds << ",\n";
    output << "      \"last_worker_active_ns\": " << counters.lastWorkerActiveNanoseconds << ",\n";
    output << "      \"worker_active_ns\": " << counters.workerActiveNanoseconds << ",\n";
    output << "      \"average_worker_active_ns\": " << counters.averageWorkerActiveNanoseconds << ",\n";
    output << "      \"last_worker_capacity_ns\": " << counters.lastWorkerCapacityNanoseconds << ",\n";
    output << "      \"worker_capacity_ns\": " << counters.workerCapacityNanoseconds << ",\n";
    output << "      \"last_worker_utilization_percent\": " << counters.lastWorkerUtilizationPercent << ",\n";
    output << "      \"average_worker_utilization_percent\": " << counters.averageWorkerUtilizationPercent << '\n';
}

void WriteSystemCounters(std::ostream& output, const SystemSchedulerSystemCounters& counters) {
    output << "    {\n";
    output << "      \"system_name\": ";
    WriteJsonString(output, counters.systemName);
    output << ",\n";
    output << "      \"execution_path\": ";
    WriteJsonString(output, counters.executionPath);
    output << ",\n";
    output << "      \"system_index\": " << counters.systemIndex << ",\n";
    output << "      \"cpu_time_ns\": " << counters.cpuTimeNanoseconds << ",\n";
    output << "      \"jobs_count\": " << counters.jobsCount << ",\n";
    output << "      \"chunk_jobs_count\": " << counters.chunkJobsCount << ",\n";
    output << "      \"entities_processed\": " << counters.entitiesProcessed << ",\n";
    output << "      \"bytes_touched\": " << counters.bytesTouched << '\n';
    output << "    }";
}

void WriteStageCounters(std::ostream& output, const SystemSchedulerStageCounters& counters) {
    output << "    {\n";
    output << "      \"stage_index\": " << counters.stageIndex << ",\n";
    output << "      \"system_count\": " << counters.systemCount << ",\n";
    output << "      \"cpu_time_ns\": " << counters.cpuTimeNanoseconds << ",\n";
    output << "      \"jobs_count\": " << counters.jobsCount << ",\n";
    output << "      \"chunk_jobs_count\": " << counters.chunkJobsCount << ",\n";
    output << "      \"wait_time_ns\": " << counters.waitTimeNanoseconds << ",\n";
    output << "      \"worker_busy_time_ns\": " << counters.workerBusyTimeNanoseconds << '\n';
    output << "    }";
}

void WriteWorkerCounters(std::ostream& output, const SystemSchedulerWorkerTrace& worker) {
    output << "    {\n";
    output << "      \"worker_index\": " << worker.workerIndex << ",\n";
    output << "      \"busy_time_ns\": " << worker.busyTimeNanoseconds << ",\n";
    output << "      \"idle_time_ns\": " << worker.idleTimeNanoseconds << ",\n";
    output << "      \"utilization_permille\": " << worker.utilizationPermille << '\n';
    output << "    }";
}

void WriteStringArray(std::ostream& output, const std::vector<std::string>& values) {
    output << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0U) {
            output << ", ";
        }
        WriteJsonString(output, values[index]);
    }
    output << ']';
}

void WriteEvent(std::ostream& output, const SystemSchedulerTraceEvent& event) {
    output << "    {\n";
    output << "      \"system_name\": ";
    WriteJsonString(output, event.systemName);
    output << ",\n";
    output << "      \"execution_path\": ";
    WriteJsonString(output, event.executionPath);
    output << ",\n";
    output << "      \"system_index\": " << event.systemIndex << ",\n";
    output << "      \"stage_index\": " << event.stageIndex << ",\n";
    output << "      \"worker_index\": " << event.workerIndex << ",\n";
    output << "      \"jobs_count\": " << event.jobsCount << ",\n";
    output << "      \"chunk_jobs_count\": " << event.chunkJobsCount << ",\n";
    output << "      \"start_time_ns\": " << event.startTimeNanoseconds << ",\n";
    output << "      \"end_time_ns\": " << event.endTimeNanoseconds << ",\n";
    output << "      \"duration_ns\": " << event.durationNanoseconds << ",\n";
    output << "      \"wait_time_ns\": " << event.waitTimeNanoseconds << ",\n";
    output << "      \"entities_processed\": " << event.entitiesProcessed << ",\n";
    output << "      \"bytes_touched\": " << event.bytesTouched << ",\n";
    output << "      \"wait_reason\": ";
    WriteJsonString(output, event.waitReason);
    output << ",\n";
    output << "      \"blocked_dependencies\": ";
    WriteStringArray(output, event.blockedDependencies);
    output << '\n';
    output << "    }";
}

void WriteChromeTraceEvent(std::ostream& output, const SystemSchedulerTraceEvent& event) {
    output << "    {\n";
    output << "      \"name\": ";
    WriteJsonString(output, event.systemName);
    output << ",\n";
    output << "      \"cat\": \"ecs.system\",\n";
    output << "      \"ph\": \"X\",\n";
    output << "      \"pid\": 1,\n";
    output << "      \"tid\": " << event.workerIndex << ",\n";
    output << "      \"ts\": " << NanosecondsToMicroseconds(event.startTimeNanoseconds) << ",\n";
    output << "      \"dur\": " << std::max<std::uint64_t>(1U, NanosecondsToMicroseconds(event.durationNanoseconds)) << ",\n";
    output << "      \"args\": {\n";
    output << "        \"system_index\": " << event.systemIndex << ",\n";
    output << "        \"execution_path\": ";
    WriteJsonString(output, event.executionPath);
    output << ",\n";
    output << "        \"stage_index\": " << event.stageIndex << ",\n";
    output << "        \"jobs_count\": " << event.jobsCount << ",\n";
    output << "        \"chunk_jobs_count\": " << event.chunkJobsCount << ",\n";
    output << "        \"entities_processed\": " << event.entitiesProcessed << ",\n";
    output << "        \"bytes_touched\": " << event.bytesTouched << ",\n";
    output << "        \"wait_time_ns\": " << event.waitTimeNanoseconds << ",\n";
    output << "        \"wait_reason\": ";
    WriteJsonString(output, event.waitReason);
    output << "\n";
    output << "      }\n";
    output << "    }";
}

} // namespace

void ExportSystemSchedulerTraceToJsonFile(const SystemSchedulerTrace& trace, const std::filesystem::path& path) {
    if (path.empty()) {
        throw std::invalid_argument("ECS scheduler trace export path is empty");
    }
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        throw std::runtime_error("Failed to open ECS scheduler trace export file: " + path.string());
    }

    output << "{\n";
    output << "  \"schema\": \"kb.ecs.scheduler_trace.v1\",\n";
    output << "  \"frame_index\": " << trace.frameIndex << ",\n";
    output << "  \"frame_duration_ns\": " << trace.frameDurationNanoseconds << ",\n";
    output << "  \"frame_counters\": {\n";
    WriteFrameCounters(output, trace.frameCounters);
    output << "  },\n";

    output << "  \"stage_counters\": [\n";
    for (std::size_t index = 0; index < trace.stageCounters.size(); ++index) {
        WriteStageCounters(output, trace.stageCounters[index]);
        output << (index + 1U == trace.stageCounters.size() ? "\n" : ",\n");
    }
    output << "  ],\n";

    output << "  \"system_counters\": [\n";
    for (std::size_t index = 0; index < trace.systemCounters.size(); ++index) {
        WriteSystemCounters(output, trace.systemCounters[index]);
        output << (index + 1U == trace.systemCounters.size() ? "\n" : ",\n");
    }
    output << "  ],\n";

    output << "  \"workers\": [\n";
    for (std::size_t index = 0; index < trace.workers.size(); ++index) {
        WriteWorkerCounters(output, trace.workers[index]);
        output << (index + 1U == trace.workers.size() ? "\n" : ",\n");
    }
    output << "  ],\n";

    output << "  \"events\": [\n";
    for (std::size_t index = 0; index < trace.events.size(); ++index) {
        WriteEvent(output, trace.events[index]);
        output << (index + 1U == trace.events.size() ? "\n" : ",\n");
    }
    output << "  ],\n";

    output << "  \"chrome_trace_events\": [\n";
    for (std::size_t index = 0; index < trace.events.size(); ++index) {
        WriteChromeTraceEvent(output, trace.events[index]);
        output << (index + 1U == trace.events.size() ? "\n" : ",\n");
    }
    output << "  ]\n";
    output << "}\n";

    if (!output.good()) {
        throw std::runtime_error("Failed to write ECS scheduler trace export file: " + path.string());
    }
}

} // namespace kb::ecs
