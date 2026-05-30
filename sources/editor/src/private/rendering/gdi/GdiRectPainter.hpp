#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

class GdiRectPainter {
public:
    GdiRectPainter() = delete;

    static void Fill(HDC dc, const RECT& rect, COLORREF color);
};

#endif

} // namespace kb::editor
