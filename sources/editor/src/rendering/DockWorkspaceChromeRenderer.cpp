#include "rendering/DockWorkspaceChromeRenderer.hpp"

#if defined(_WIN32)
#include "rendering/DockPanelFramePainter.hpp"
#include "rendering/EditorSurfacePainter.hpp"
#include "rendering/GdiDrawing.hpp"

namespace kb::editor {

void DockWorkspaceChromeRenderer::Paint(HDC dc, const DockLayout& layout, const EditorDockModel& dockModel, const EditorTheme& theme) const {
    PaintLeaves(dc, layout, dockModel, theme);
}

void DockWorkspaceChromeRenderer::PaintSplitters(HDC dc, const DockLayout& layout, const EditorTheme& theme) {
    static_cast<void>(dc);
    static_cast<void>(layout);
    static_cast<void>(theme);
}

void DockWorkspaceChromeRenderer::PaintLeaves(HDC dc, const DockLayout& layout, const EditorDockModel& dockModel, const EditorTheme& theme) {
    for (const DockLeafLayout& leaf : layout.leaves) {
        const DockPanel* activePanel = dockModel.Queries().FindPanel(leaf.activePanelId);
        if (activePanel == nullptr) {
            continue;
        }

        DockPanelFramePainter{}.Paint(dc, GdiDrawing::ToRect(leaf.frame), activePanel->kind, theme);

        EditorSurfacePainter::Fill(dc, GdiDrawing::ToRect(leaf.tabStrip), theme, EditorSurfaceKind::HeaderStrip);
    }
}

} // namespace kb::editor

#endif
