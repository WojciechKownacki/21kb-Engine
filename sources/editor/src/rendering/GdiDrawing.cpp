#include "rendering/GdiDrawing.hpp"

#if defined(_WIN32)

namespace kb::editor {

RECT GdiDrawing::Inset(RECT rect, int amount) {
    rect.left += amount;
    rect.top += amount;
    rect.right -= amount;
    rect.bottom -= amount;
    return rect;
}

RECT GdiDrawing::ToRect(const DockRect& rect) {
    return RECT{ rect.x, rect.y, rect.x + rect.width, rect.y + rect.height };
}

void GdiDrawing::FillRectColor(HDC dc, const RECT& rect, COLORREF color) {
    ScopedBrush brush(color);
    FillRect(dc, &rect, brush.handle);
}

void GdiDrawing::FillRectAlpha(HDC target, const RECT& rect, COLORREF color, BYTE alpha) {
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

void GdiDrawing::DrawTextBlock(HDC dc, RECT rect, const char* text, COLORREF color) {
    SetTextColor(dc, color);
    DrawTextA(dc, text, -1, &rect, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK);
}

void GdiDrawing::DrawTabText(HDC dc, RECT rect, const char* text, COLORREF color) {
    SetTextColor(dc, color);
    DrawTextA(dc, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
}

void GdiDrawing::DrawCenteredText(HDC dc, RECT rect, const char* text, COLORREF color) {
    SetTextColor(dc, color);
    DrawTextA(dc, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

void GdiDrawing::DrawSharpFrame(HDC dc, const RECT& rect, COLORREF fill, COLORREF border) {
    FillRectColor(dc, rect, fill);

    ScopedPen borderPen(1, border);
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, borderPen.handle));
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
    Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
}

} // namespace kb::editor

#endif
