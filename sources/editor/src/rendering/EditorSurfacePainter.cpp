#include "rendering/EditorSurfacePainter.hpp"

#if defined(_WIN32)
#include "rendering/EditorSurfaceStyle.hpp"
#include "rendering/GdiDrawing.hpp"

namespace kb::editor {

void EditorSurfacePainter::Fill(HDC dc, const RECT& rect, const EditorTheme& theme, EditorSurfaceKind kind) {
    GdiDrawing::FillRectColor(dc, rect, EditorSurfaceStyle::FillColor(theme, kind));
}

void EditorSurfacePainter::Frame(HDC dc, const RECT& rect, const EditorTheme& theme, EditorSurfaceKind kind, COLORREF border) {
    GdiDrawing::DrawSharpFrame(dc, rect, EditorSurfaceStyle::FillColor(theme, kind), border);
}

} // namespace kb::editor

#endif
