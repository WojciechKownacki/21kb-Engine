#pragma once

#include "profiler/EditorProfilerState.hpp"

namespace kb::scene {
class Scene;
}

namespace kb::editor {

// Publishes one presented frame to the registry. Only the areas the selection measures are
// queried, and a value the machine cannot produce (no GPU timer, no video memory figure) is not
// published, so its counter stays unavailable instead of showing a made-up number.
class EditorProfilerSampler final {
public:
    EditorProfilerSampler() = delete;

    static void Sample(EditorProfilerState& profiler, const ProfilerSelection& selection,
        const kb::scene::Scene& scene, double frameMilliseconds);
};

} // namespace kb::editor
