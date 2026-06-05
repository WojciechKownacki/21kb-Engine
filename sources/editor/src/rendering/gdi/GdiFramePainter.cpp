#include "rendering/gdi/GdiFramePainter.hpp"

#include "rendering/gdi/GdiRectPainter.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"
#include "rendering/gdi/ScopedPen.hpp"

#if defined(_WIN32)

namespace kb::editor {
namespace {

void DrawBorder(HDC dc, const RECT& rect, COLORREF border) {
    ScopedPen borderPen(1, border);
    const ScopedGdiObject selectedPen(dc, borderPen.handle);
    const ScopedGdiObject selectedBrush(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
}

} // namespace

void GdiFramePainter::DrawSharp(HDC dc, const RECT& rect, COLORREF fill, COLORREF border) {
    GdiRectPainter::Fill(dc, rect, fill);
    DrawBorder(dc, rect, border);
}

void GdiFramePainter::DrawRaised(HDC dc, const RECT& rect, COLORREF fill, COLORREF highlight, COLORREF shadow) {
    GdiRectPainter::Fill(dc, rect, fill);

    ScopedPen highlightPen(1, highlight);
    {
        const ScopedGdiObject selectedPen(dc, highlightPen.handle);
        MoveToEx(dc, rect.left, rect.bottom - 1, nullptr);
        LineTo(dc, rect.left, rect.top);
        LineTo(dc, rect.right - 1, rect.top);
    }

    ScopedPen shadowPen(1, shadow);
    {
        const ScopedGdiObject selectedPen(dc, shadowPen.handle);
        MoveToEx(dc, rect.right - 1, rect.top, nullptr);
        LineTo(dc, rect.right - 1, rect.bottom - 1);
        LineTo(dc, rect.left, rect.bottom - 1);
    }
}

} // namespace kb::editor

#endif
