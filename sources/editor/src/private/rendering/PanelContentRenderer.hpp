#pragma once

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

class PanelContentRenderer {
public:
#if defined(_WIN32)
    void Paint(
        HDC dc,
        const RECT& content,
        const RECT& panelFrame,
        const RECT& contentClip,
        const RECT& overlayBounds,
        const DockPanel& panel,
        const EditorTheme& theme,
        const EditorMetrics& metrics,
        EditorSceneContext& sceneContext,
        const EditorRenderBackendSettings& renderBackendSettings,
        bool floating,
        EditorSceneBgfxViewport* sceneViewport = nullptr,
        HWND sceneViewportHost = nullptr) const;
#endif
};

} // namespace kb::editor
