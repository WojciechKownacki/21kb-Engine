#pragma once

#include "app/EditorPointerDragState.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

class EditorDragOverlayRenderer {
public:
    void Paint(HDC dc, const EditorPointerDragState& drag, const EditorTheme& theme, const EditorSceneContext& sceneContext) const;
};

#endif

} // namespace kb::editor
