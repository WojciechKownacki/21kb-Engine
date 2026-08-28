#pragma once

#include "docking/EditorDockModel.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/EditorRenderBackendSettings.hpp"
#include "rendering/EditorSceneBgfxViewport.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class DockWorkspaceContentRenderer {
public:
#if defined(_WIN32)
    // dirty is the region the paint may actually change. A panel outside it is skipped
    // whole: the retained back buffer still holds its pixels, and its content renderer is
    // the expensive half - it rebuilds rows, thumbnails and models before drawing.
    void Paint(HWND parent, HDC dc, const RECT& dirty, const DockLayout& layout, const EditorDockModel& dockModel, const EditorTheme& theme, const EditorMetrics& metrics, EditorSceneContext& sceneContext, const EditorRenderBackendSettings& renderBackendSettings, EditorSceneBgfxViewport* sceneViewport) const;
#endif
};

} // namespace kb::editor
