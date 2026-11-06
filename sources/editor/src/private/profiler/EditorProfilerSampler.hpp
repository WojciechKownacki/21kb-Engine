#pragma once

#include "profiler/EditorProfilerRegions.hpp"
#include "profiler/EditorProfilerState.hpp"

namespace kb::scene {
class Scene;
}

namespace kb::render {
struct SceneRenderSubmitStats;
}

namespace kb::editor {

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
