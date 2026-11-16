#pragma once

#include "docking/DockPointerDrag.hpp"
#include "docking/EditorDockModel.hpp"
#include "kb/editor/docking/DockTypes.hpp"
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

// A floating window: the tab strip of its group, its window controls and the panel in front.
class FloatingEditorWindowRenderer {
public:
#if defined(_WIN32)
    void Paint(HDC dc, HWND window, const RECT& client, const DockPanel& panel, const EditorDockModel& dockModel, const DockPointerDrag* dockDrag, const EditorTheme& theme, const EditorMetrics& metrics, EditorSceneContext& sceneContext, const EditorRenderBackendSettings& renderBackendSettings, EditorSceneBgfxViewport* sceneViewport) const;
#endif
};

} // namespace kb::editor
