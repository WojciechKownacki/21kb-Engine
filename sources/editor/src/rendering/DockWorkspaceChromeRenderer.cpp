#include "rendering/DockWorkspaceChromeRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"

namespace kb::editor {

void DockWorkspaceChromeRenderer::Paint(HDC dc, const DockLayout& layout, const EditorDockModel& dockModel, const EditorTheme& theme) const {
    PaintSplitters(dc, layout, theme);
    PaintLeaves(dc, layout, dockModel, theme);
}

void DockWorkspaceChromeRenderer::PaintSplitters(HDC dc, const DockLayout& layout, const EditorTheme& theme) {
    for (const DockSplitterLayout& splitter : layout.splitters) {
        GdiDrawing::FillRectColor(dc, GdiDrawing::ToRect(splitter.rect), GdiDrawing::ToColorRef(theme.splitter));
    }
}

void DockWorkspaceChromeRenderer::PaintLeaves(HDC dc, const DockLayout& layout, const EditorDockModel& dockModel, const EditorTheme& theme) {
    for (const DockLeafLayout& leaf : layout.leaves) {
        const DockPanel* activePanel = dockModel.Queries().FindPanel(leaf.activePanelId);
        if (activePanel == nullptr) {
            continue;
        }

        const bool scene = activePanel->kind == DockPanelKind::Scene;
        GdiDrawing::DrawSharpFrame(dc, GdiDrawing::ToRect(leaf.frame), scene ? GdiDrawing::ToColorRef(theme.chrome) : GdiDrawing::ToColorRef(theme.panel), GdiDrawing::ToColorRef(theme.borderPanel));
        GdiDrawing::FillRectColor(dc, GdiDrawing::ToRect(leaf.tabStrip), GdiDrawing::ToColorRef(theme.strip));
    }
}

} // namespace kb::editor

#endif
