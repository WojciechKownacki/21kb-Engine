#pragma once

#include "profiler/EditorProfilerState.hpp"

#include <optional>

namespace kb::scene {
class Scene;
}

namespace kb::render {
struct SceneRenderSubmitStats;
}

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

// Publishes one presented frame to the registry, files its regions and, while recording, its
// counters. Only the areas the selection measures are queried, and a value the machine cannot
// produce (no GPU timer, no video memory figure, no game thread outside Play) is not published, so
// its counter stays unavailable instead of showing a made-up number. Nothing is sampled while the
// panel is paused.
class EditorProfilerSampler final {
public:
    EditorProfilerSampler() = delete;

    static void Sample(EditorProfilerState& profiler, const ProfilerSelection& selection, const kb::scene::Scene& scene,
        const EditorProfilerFrame& frame, const kb::render::SceneRenderSubmitStats* renderStats);
};

} // namespace kb::editor
