#include "rendering/EditorTabIndicatorPainter.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"

namespace kb::editor {

void EditorTabIndicatorPainter::PaintActive(HDC dc, const RECT& tabRect, const EditorTheme& theme) {
    RECT indicator{ tabRect.left + 1, tabRect.top + 1, tabRect.right - 1, tabRect.top + 3 };
    GdiDrawing::FillRectColor(dc, indicator, GdiDrawing::ToColorRef(theme.accent));
}

} // namespace kb::editor

#endif
