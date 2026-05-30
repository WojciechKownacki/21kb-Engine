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

class EditorSurfacePainter {
public:
    EditorSurfacePainter() = delete;

    static void Fill(HDC dc, const RECT& rect, const EditorTheme& theme, EditorSurfaceKind kind);
    static void Frame(HDC dc, const RECT& rect, const EditorTheme& theme, EditorSurfaceKind kind, COLORREF border);
};

#endif

} // namespace kb::editor
