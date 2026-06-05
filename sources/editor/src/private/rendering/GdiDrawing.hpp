#pragma once

#include "kb/editor/docking/DockTypes.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/GdiResources.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

class GdiDrawing {
public:
    GdiDrawing() = delete;

    [[nodiscard]] static COLORREF ToColorRef(EditorColor color);

    [[nodiscard]] static RECT Inset(RECT rect, int amount);
    [[nodiscard]] static RECT ToRect(const DockRect& rect);

    static void FillRectColor(HDC dc, const RECT& rect, COLORREF color);
    static void FillRectAlpha(HDC target, const RECT& rect, COLORREF color, BYTE alpha);
    static void DrawTextBlock(HDC dc, RECT rect, const char* text, COLORREF color);
    static void DrawTabText(HDC dc, RECT rect, const char* text, COLORREF color);
    static void DrawCenteredText(HDC dc, RECT rect, const char* text, COLORREF color);
    static void DrawSharpFrame(HDC dc, const RECT& rect, COLORREF fill, COLORREF border);
    static void DrawRaisedFrame(HDC dc, const RECT& rect, COLORREF fill, COLORREF highlight, COLORREF shadow);
};

#endif

} // namespace kb::editor
