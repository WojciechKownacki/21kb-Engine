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

class FloatingEditorWindowRenderer {
public:
#if defined(_WIN32)
    void Paint(HDC dc, HWND window, const RECT& client, const DockPanel& panel, const EditorTheme& theme, const EditorMetrics& metrics, const EditorSceneContext& sceneContext, EditorSceneBgfxViewport& sceneViewport) const;
#endif
};

} // namespace kb::editor
