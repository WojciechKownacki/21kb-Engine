#pragma once

#include "app/EditorPointerDragState.hpp"
#include "docking/EditorDockModel.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/EditorSceneBgfxViewport.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class MainWindowBackBufferPainter {
public:
#if defined(_WIN32)
    static void Paint(HWND window, const EditorDockModel& dockModel, const EditorTheme& theme, const EditorMetrics& metrics, const EditorSceneContext& sceneContext, const DockDropPreview* preview, const EditorPointerDragState& drag, EditorSceneBgfxViewport& sceneViewport);
#endif
};

} // namespace kb::editor
