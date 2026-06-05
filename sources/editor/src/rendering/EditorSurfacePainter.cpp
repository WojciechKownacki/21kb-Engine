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

void EditorSurfacePainter::RaisedFrame(HDC dc, const RECT& rect, const EditorTheme& theme, EditorSurfaceKind kind, COLORREF highlight, COLORREF shadow) {
    GdiDrawing::DrawRaisedFrame(dc, rect, EditorSurfaceStyle::FillColor(theme, kind), highlight, shadow);
}

} // namespace kb::editor

#endif
