#pragma once

#include "kb/editor/docking/DockTypes.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
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
    void Paint(
        HDC dc,
        const RECT& content,
        const DockPanel& panel,
        const EditorTheme& theme,
        const EditorSceneContext& sceneContext,
        EditorSceneBgfxViewport* sceneViewport,
        HWND sceneViewportHost) const;
#endif
};

} // namespace kb::editor
