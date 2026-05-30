#pragma once

#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

class EditorTabIndicatorPainter {
public:
    EditorTabIndicatorPainter() = delete;

    static void PaintActive(HDC dc, const RECT& tabRect, const EditorTheme& theme);
};

#endif

} // namespace kb::editor
