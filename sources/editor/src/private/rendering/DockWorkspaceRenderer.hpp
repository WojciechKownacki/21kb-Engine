#pragma once

#include "docking/EditorDockModel.hpp"
#include "app/EditorPlayModeState.hpp"
#include "app/EditorShellInteractionState.hpp"
#include "docking/DockPointerDrag.hpp"
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

class DockWorkspaceRenderer {
public:
#if defined(_WIN32)
    void Paint(HWND parent, HDC dc, const RECT& dirty, int width, int height, const EditorDockModel& dockModel, const EditorTheme& theme, const EditorMetrics& metrics, EditorSceneContext& sceneContext, const EditorRenderBackendSettings& renderBackendSettings, const DockDropPreview* preview, const DockPointerDrag* dockDrag, const EditorPlayModeState& playMode, const EditorShellInteractionState& shellInteraction, EditorSceneBgfxViewport* sceneViewport) const;
#endif
};

} // namespace kb::editor
