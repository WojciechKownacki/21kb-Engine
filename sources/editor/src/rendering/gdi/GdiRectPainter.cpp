#include "rendering/gdi/GdiRectPainter.hpp"

#include "rendering/gdi/ScopedBrush.hpp"

#if defined(_WIN32)

namespace kb::editor {

void GdiRectPainter::Fill(HDC dc, const RECT& rect, COLORREF color) {
    ScopedBrush brush(color);
    FillRect(dc, &rect, brush.handle);
}

} // namespace kb::editor

#endif
