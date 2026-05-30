#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/EditorSurfaceKind.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

class EditorSurfaceStyle {
public:
    EditorSurfaceStyle() = delete;

    [[nodiscard]] static COLORREF FillColor(const EditorTheme& theme, EditorSurfaceKind kind);
};

#endif

} // namespace kb::editor
