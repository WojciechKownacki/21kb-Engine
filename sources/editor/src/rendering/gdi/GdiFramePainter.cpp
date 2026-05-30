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

} // namespace kb::editor

#endif
