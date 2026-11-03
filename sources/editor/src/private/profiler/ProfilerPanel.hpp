#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

// The Profiler panel: a left pane with the areas and counters to choose, and three tabs.
// Measurements has a chart for each measured area, the frame to look at, and the regions of that
// frame; Counters a table with every counter's readings and thresholds; Diagnosis what the engine
// found in the measurements and what to do about it.
class ProfilerPanel final {
public:
    ProfilerPanel() = delete;

    static void Paint(HDC dc, const RECT& content, const EditorTheme& theme, const EditorSceneContext& sceneContext);
    // A left click at (x, y); true when the panel took it. `owner` hosts the dialog that edits a threshold.
    static bool Pointer(HWND owner, const RECT& content, int x, int y, EditorSceneContext& sceneContext);
    // A turn of the mouse wheel over the panel; true when something scrolled.
    static bool Wheel(const RECT& content, int x, int y, int delta, EditorSceneContext& sceneContext);
};

#endif

} // namespace kb::editor
