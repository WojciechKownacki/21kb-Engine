#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

class GdiTextPainter {
public:
    GdiTextPainter() = delete;

    static void DrawBlock(HDC dc, RECT rect, const char* text, COLORREF color);
    static void DrawTab(HDC dc, RECT rect, const char* text, COLORREF color);
    static void DrawCentered(HDC dc, RECT rect, const char* text, COLORREF color);
};

#endif

} // namespace kb::editor
