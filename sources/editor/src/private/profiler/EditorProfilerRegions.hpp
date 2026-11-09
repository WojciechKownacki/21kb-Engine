#pragma once

#include "engine/core/ProfilerCounters.hpp"
#include "engine/ecs/SystemSchedulerTrace.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace kb::editor {

// What one presented frame cost, as the editor's loop measured it.
struct EditorProfilerFrame {
    double presentMilliseconds = 0.0;
    // The stages of the present: laying out, queueing the main window's viewports and the floating
    // windows', and ending the frame.
    double beginMilliseconds = 0.0;
    double mainMilliseconds = 0.0;
    double floatingMilliseconds = 0.0;
    double endMilliseconds = 0.0;
    // Of the end of the frame, the time the renderer waited for the previous frame to finish.
    double waitMilliseconds = 0.0;
    // The scene's update before the present; empty while the scene is not simulating.
    std::optional<double> gameTickMilliseconds;
};

// How the scene's update was spent, as the runtime reports it: the update, the transform sync that
// happens inside it and the CPU time of each system of the scheduler.
struct EditorProfilerSceneTimes {
    std::uint64_t updateNanoseconds = 0U;
    std::uint64_t transformSyncNanoseconds = 0U;
    std::span<const kb::ecs::SystemSchedulerSystemCounters> systems;
};

// The frame as regions on the timeline of `regions`: the scene's update with its systems and its
// transform sync inside it, and the present with its stages and the wait inside the last of them.
// The regions follow one another from the start of the frame; their lengths are what was measured.
void RecordProfilerRegions(kb::core::ProfilerCounters& regions, const EditorProfilerFrame& frame, const EditorProfilerSceneTimes& scene);

} // namespace kb::editor
