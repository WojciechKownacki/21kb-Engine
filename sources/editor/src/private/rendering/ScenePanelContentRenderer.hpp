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

class ScenePanelContentRenderer {
public:
#if defined(_WIN32)
    static void PresentViewport(
        EditorSceneBgfxViewport& sceneViewport,
        HWND sceneViewportHost,
        const RECT& content,
        const DockPanel& panel,
        const EditorSceneContext& sceneContext,
        const EditorRenderBackendSettings& renderBackendSettings);

    void Paint(
        HDC dc,
        const RECT& content,
        const DockPanel& panel,
        const EditorTheme& theme,
        const EditorSceneContext& sceneContext,
        const EditorRenderBackendSettings& renderBackendSettings,
        EditorSceneBgfxViewport* sceneViewport,
        HWND sceneViewportHost) const;
#endif
};

} // namespace kb::editor
