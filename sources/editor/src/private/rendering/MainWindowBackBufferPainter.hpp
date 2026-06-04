#pragma once

#include "app/EditorPlayModeState.hpp"
#include "app/EditorPointerDragState.hpp"
#include "app/EditorShellInteractionState.hpp"
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

class MainWindowBackBufferPainter {
public:
#if defined(_WIN32)
    static void Paint(HWND window, const EditorDockModel& dockModel, const EditorTheme& theme, const EditorMetrics& metrics, const EditorSceneContext& sceneContext, const DockDropPreview* preview, const EditorPointerDragState& drag, const EditorRenderBackendSettings& renderBackendSettings, const EditorPlayModeState& playMode, const EditorShellInteractionState& shellInteraction, EditorSceneBgfxViewport& sceneViewport);
#endif
};

} // namespace kb::editor
