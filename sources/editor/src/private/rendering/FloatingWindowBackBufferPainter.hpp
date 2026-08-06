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

class FloatingWindowBackBufferPainter {
public:
#if defined(_WIN32)
    static void Paint(HWND window, const DockPanel& panel, const EditorTheme& theme, const EditorMetrics& metrics, EditorSceneContext& sceneContext, const EditorRenderBackendSettings& renderBackendSettings, EditorSceneBgfxViewport& sceneViewport);
    // Hides every overlay popup this painter owns. Called on WM_ACTIVATEAPP
    // deactivation so no owned popup stays visible above another application;
    // the next Paint after reactivation re-shows whatever the UI state needs.
    static void HideAllOverlays() noexcept;
#endif
};

} // namespace kb::editor
