#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class HierarchyPanelRenderer {
public:
#if defined(_WIN32)
    void Paint(HDC dc, const RECT& content, const EditorTheme& theme, const EditorSceneContext& sceneContext) const;
    void PaintContextMenu(HDC dc, const RECT& bounds, const EditorTheme& theme, const EditorSceneContext& sceneContext) const;
#endif
};

} // namespace kb::editor
