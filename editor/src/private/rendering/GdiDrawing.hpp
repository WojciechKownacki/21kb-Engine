#pragma once

#include "kb/editor/docking/DockTypes.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <algorithm>

namespace kb::editor {

#if defined(_WIN32)

struct ScopedBrush {
    explicit ScopedBrush(COLORREF color)
        : handle(CreateSolidBrush(color)) {
    }

    ~ScopedBrush() {
        if (handle != nullptr) {
            DeleteObject(handle);
        }
    }

    ScopedBrush(const ScopedBrush&) = delete;
    ScopedBrush& operator=(const ScopedBrush&) = delete;

    HBRUSH handle = nullptr;
};

struct ScopedFont {
    ScopedFont(int pointSize, int weight)
        : handle(CreateFontW(
              -pointSize,
              0,
              0,
              0,
              weight,
              FALSE,
              FALSE,
              FALSE,
              DEFAULT_CHARSET,
              OUT_DEFAULT_PRECIS,
              CLIP_DEFAULT_PRECIS,
              CLEARTYPE_QUALITY,
              DEFAULT_PITCH | FF_DONTCARE,
              L"Segoe UI")) {
    }

    ~ScopedFont() {
        if (handle != nullptr) {
            DeleteObject(handle);
        }
    }

    ScopedFont(const ScopedFont&) = delete;
    ScopedFont& operator=(const ScopedFont&) = delete;

    HFONT handle = nullptr;
};

struct ScopedPen {
    ScopedPen(int width, COLORREF color)
        : handle(CreatePen(PS_SOLID, width, color)) {
    }

    ~ScopedPen() {
        if (handle != nullptr) {
            DeleteObject(handle);
        }
    }

    ScopedPen(const ScopedPen&) = delete;
    ScopedPen& operator=(const ScopedPen&) = delete;

    HPEN handle = nullptr;
};

struct ScopedCompatibleDc {
    explicit ScopedCompatibleDc(HDC source)
        : handle(CreateCompatibleDC(source)) {
    }

    ~ScopedCompatibleDc() {
        if (handle != nullptr) {
            DeleteDC(handle);
        }
    }

    ScopedCompatibleDc(const ScopedCompatibleDc&) = delete;
    ScopedCompatibleDc& operator=(const ScopedCompatibleDc&) = delete;

    HDC handle = nullptr;
};

struct ScopedBitmap {
    ScopedBitmap(HDC source, int width, int height)
        : handle(CreateCompatibleBitmap(source, std::max(1, width), std::max(1, height))) {
    }

    ~ScopedBitmap() {
        if (handle != nullptr) {
            DeleteObject(handle);
        }
    }

    ScopedBitmap(const ScopedBitmap&) = delete;
    ScopedBitmap& operator=(const ScopedBitmap&) = delete;

    HBITMAP handle = nullptr;
};

class GdiDrawing {
public:
    GdiDrawing() = delete;

    [[nodiscard]] static constexpr COLORREF ToColorRef(EditorColor color) {
        return RGB(color.r, color.g, color.b);
    }

    [[nodiscard]] static RECT Inset(RECT rect, int amount) {
        rect.left += amount;
        rect.top += amount;
        rect.right -= amount;
        rect.bottom -= amount;
        return rect;
    }

    [[nodiscard]] static RECT ToRect(const DockRect& rect) {
        return RECT{ rect.x, rect.y, rect.x + rect.width, rect.y + rect.height };
    }

    static void FillRectColor(HDC dc, const RECT& rect, COLORREF color) {
        ScopedBrush brush(color);
        FillRect(dc, &rect, brush.handle);
    }

    static void FillRectAlpha(HDC target, const RECT& rect, COLORREF color, BYTE alpha) {
        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;
        if (width <= 0 || height <= 0) {
            return;
        }

        ScopedCompatibleDc overlayDc(target);
        ScopedBitmap overlayBitmap(target, width, height);
        HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(overlayDc.handle, overlayBitmap.handle));
        RECT overlayRect{ 0, 0, width, height };
        FillRectColor(overlayDc.handle, overlayRect, color);

        BLENDFUNCTION blend{};
        blend.BlendOp = AC_SRC_OVER;
        blend.SourceConstantAlpha = alpha;
        AlphaBlend(target, rect.left, rect.top, width, height, overlayDc.handle, 0, 0, width, height, blend);
        SelectObject(overlayDc.handle, oldBitmap);
    }

    static void DrawTextBlock(HDC dc, RECT rect, const char* text, COLORREF color) {
        SetTextColor(dc, color);
        DrawTextA(dc, text, -1, &rect, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK);
    }

    static void DrawTabText(HDC dc, RECT rect, const char* text, COLORREF color) {
        SetTextColor(dc, color);
        DrawTextA(dc, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }

    static void DrawCenteredText(HDC dc, RECT rect, const char* text, COLORREF color) {
        SetTextColor(dc, color);
        DrawTextA(dc, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    static void DrawSharpFrame(HDC dc, const RECT& rect, COLORREF fill, COLORREF border) {
        FillRectColor(dc, rect, fill);

        ScopedPen borderPen(1, border);
        HPEN oldPen = static_cast<HPEN>(SelectObject(dc, borderPen.handle));
        HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
        Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
    }
};

#endif

} // namespace kb::editor
