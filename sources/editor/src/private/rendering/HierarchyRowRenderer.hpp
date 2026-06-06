#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "scene/EditorSceneContext.hpp"
#include "scene/EditorHierarchyRow.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class HierarchyRowRenderer {
public:
#if defined(_WIN32)
    void Paint(HDC dc, const RECT& rowRect, const EditorTheme& theme, const EditorHierarchyRow& row, const EditorSceneContext& sceneContext) const;
#endif
};

} // namespace kb::editor
