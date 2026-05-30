#include "rendering/EditorToolbarRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"

namespace kb::editor {

void EditorToolbarRenderer::PaintMenu(HDC dc, const RECT& rect, const EditorTheme& theme) const {
    GdiDrawing::FillRectColor(dc, rect, GdiDrawing::ToColorRef(theme.menuBar));
    GdiDrawing::DrawTextBlock(dc, { 20, 10, 220, 34 }, "21kb Engine", GdiDrawing::ToColorRef(theme.textPrimary));
    GdiDrawing::DrawTextBlock(dc, { 140, 10, 420, 34 }, "Docking workspace", GdiDrawing::ToColorRef(theme.textSecondary));
}

void EditorToolbarRenderer::PaintToolbar(HDC dc, const RECT& rect, const EditorTheme& theme) const {
    GdiDrawing::FillRectColor(dc, rect, GdiDrawing::ToColorRef(theme.toolbar));
    PaintButton(dc, { 16, rect.top + 9, 92, rect.bottom - 9 }, "Select", theme, true);
    PaintButton(dc, { 100, rect.top + 9, 176, rect.bottom - 9 }, "Move", theme, false);
    PaintButton(dc, { 184, rect.top + 9, 268, rect.bottom - 9 }, "Rotate", theme, false);
    PaintButton(dc, { 276, rect.top + 9, 352, rect.bottom - 9 }, "Scale", theme, false);
}

void EditorToolbarRenderer::PaintButton(HDC dc, const RECT& rect, const char* label, const EditorTheme& theme, bool active) const {
    GdiDrawing::DrawSharpFrame(
        dc,
        rect,
        active ? GdiDrawing::ToColorRef(theme.tabActive) : GdiDrawing::ToColorRef(theme.toolbarButton),
        active ? GdiDrawing::ToColorRef(theme.accent) : GdiDrawing::ToColorRef(theme.borderChrome));
    GdiDrawing::DrawTextBlock(
        dc,
        { rect.left + 12, rect.top + 7, rect.right - 12, rect.bottom - 4 },
        label,
        active ? GdiDrawing::ToColorRef(theme.textPrimary) : GdiDrawing::ToColorRef(theme.textSecondary));
}

} // namespace kb::editor

#endif
