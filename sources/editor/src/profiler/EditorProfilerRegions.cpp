#include "profiler/EditorProfilerRegions.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace kb::editor {
namespace {

using kb::core::ProfilerCounters;

constexpr double kNanosecondsPerMillisecond = 1'000'000.0;

[[nodiscard]] std::uint64_t Nanoseconds(double milliseconds) {
    return static_cast<std::uint64_t>(std::max(0.0, milliseconds) * kNanosecondsPerMillisecond);
}

} // namespace

void RecordProfilerRegions(ProfilerCounters& regions, const EditorProfilerFrame& frame, const EditorProfilerSceneTimes& scene) {
    const std::uint64_t start = ProfilerCounters::NowNanoseconds();
    const std::uint64_t game = frame.gameTickMilliseconds ? Nanoseconds(*frame.gameTickMilliseconds) : 0U;
    const std::uint64_t present = Nanoseconds(frame.presentMilliseconds);
    regions.RecordRegion("Editor frame", start, game + present, 0U);
    std::uint64_t cursor = start;
    if (frame.gameTickMilliseconds) {
        regions.RecordRegion("Game tick", cursor, game, 1U);
        const std::uint64_t update = std::min(scene.updateNanoseconds, game);
        // The transform sync is part of the update, not a step after it.
        const std::uint64_t transform = std::min(scene.transformSyncNanoseconds, update);
        const std::uint64_t systemsTime = update - transform;
        regions.RecordRegion("Scene update", cursor, update, 2U);
        // Systems run on several threads, so their CPU times can add up to more than the time the
        // update took; then each shows its share of that time, which keeps every system inside it.
        std::uint64_t cpuTotal = 0U;
        for (const kb::ecs::SystemSchedulerSystemCounters& system : scene.systems) cpuTotal += system.cpuTimeNanoseconds;
        std::uint64_t systemStart = cursor;
        for (const kb::ecs::SystemSchedulerSystemCounters& system : scene.systems) {
            const std::uint64_t length = cpuTotal <= systemsTime ? system.cpuTimeNanoseconds
                : static_cast<std::uint64_t>(static_cast<double>(system.cpuTimeNanoseconds) * static_cast<double>(systemsTime) / static_cast<double>(cpuTotal));
            regions.RecordRegion(system.systemName, systemStart, length, 3U);
            systemStart += length;
        }
        regions.RecordRegion("Transform sync", cursor + systemsTime, transform, 3U);
        cursor += game;
    }
    regions.RecordRegion("Present", cursor, present, 1U);
    std::uint64_t stageStart = cursor;
    const std::array<std::pair<const char*, double>, 4> stages{ {
        { "Begin layout", frame.beginMilliseconds }, { "Queue main window", frame.mainMilliseconds },
        { "Queue floating windows", frame.floatingMilliseconds }, { "End frame", frame.endMilliseconds } } };
    std::uint64_t endStart = stageStart;
    std::uint64_t endLength = 0U;
    for (const auto& [name, milliseconds] : stages) {
        // The stages stay inside the present, whatever the rounding of the timers adds up to.
        const std::uint64_t length = std::min(Nanoseconds(milliseconds), present - (stageStart - cursor));
        regions.RecordRegion(name, stageStart, length, 2U);
        endStart = stageStart;
        endLength = length;
        stageStart += length;
    }
    // bgfx::frame waits for the render thread inside the end of the frame, and the wait is no work.
    regions.RecordRegion("Wait for the previous frame", endStart, std::min(Nanoseconds(frame.waitMilliseconds), endLength), 3U, true);
}

} // namespace kb::editor
