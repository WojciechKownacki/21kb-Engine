#include "rendering/gdi/GdiFramePainter.hpp"

#include "rendering/gdi/GdiRectPainter.hpp"
#include "rendering/gdi/ScopedPen.hpp"

#if defined(_WIN32)

namespace kb::editor {

void GdiFramePainter::DrawSharp(HDC dc, const RECT& rect, COLORREF fill, COLORREF border) {
    GdiRectPainter::Fill(dc, rect, fill);

    ScopedPen borderPen(1, border);
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, borderPen.handle));
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
    Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
}

} // namespace kb::editor

#endif
