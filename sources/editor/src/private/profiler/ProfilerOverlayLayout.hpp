#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "profiler/ProfilerOverlayModel.hpp"
#include "rendering/EditorSceneViewportTextOverlay.hpp"

#include <vector>

namespace kb::editor {

struct ProfilerOverlayContent {
    std::vector<EditorSceneViewportTextLabel> labels;
    std::vector<EditorSceneViewportOverlayShape> shapes;
};

// Sizes in pixels of one overlay; `scale` grows the list and the text, the graph is sized apart so
// a small preview can draw the same overlay.
struct ProfilerOverlayMetrics {
    float scale = 1.0F;
    float graphWidth = 372.0F;
    float graphHeight = 186.0F;

    [[nodiscard]] static ProfilerOverlayMetrics ForTextSize(ProfilerTextSize size) noexcept;
};

// Lays the model out as panels, lines and labels in a viewport of the given size, in the colors of
// the theme.
void AppendProfilerOverlay(ProfilerOverlayContent& content, const ProfilerOverlayModel& model,
    const ProfilerOverlayOptions& options, const EditorTheme& theme, const ProfilerOverlayMetrics& metrics,
    float viewportWidth, float viewportHeight);

} // namespace kb::editor
