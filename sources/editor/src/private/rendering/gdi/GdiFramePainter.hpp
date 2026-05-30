#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

class GdiFramePainter {
public:
    GdiFramePainter() = delete;

    static void DrawSharp(HDC dc, const RECT& rect, COLORREF fill, COLORREF border);
};

#endif

} // namespace kb::editor
