#include "rendering/DockWorkspaceTabStripRenderer.hpp"

#if defined(_WIN32)
#include "rendering/EditorSurfacePainter.hpp"
#include "rendering/EditorTabIndicatorPainter.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

namespace kb::editor {

void DockWorkspaceTabStripRenderer::Paint(HDC dc, const DockLayout& layout, const EditorDockModel& dockModel, const EditorTheme& theme) const {
    ScopedFont titleFont(16, FW_SEMIBOLD);
    const ScopedGdiObject selectedFont(dc, titleFont.handle);

    for (const DockPanelLayout& panelLayout : layout.panels) {
        const DockPanel* panel = dockModel.Queries().FindPanel(panelLayout.panelId);
        if (panel == nullptr) {
            continue;
        }

        EditorSurfacePainter::Frame(
            dc,
            GdiDrawing::ToRect(panelLayout.tab),
            theme,
            panelLayout.active ? EditorSurfaceKind::ActiveTab : EditorSurfaceKind::InactiveTab,
            panelLayout.active ? GdiDrawing::ToColorRef(theme.borderPanel) : GdiDrawing::ToColorRef(theme.strip));
        if (panelLayout.active) {
            EditorTabIndicatorPainter::PaintActive(dc, GdiDrawing::ToRect(panelLayout.tab), theme);
        }

        RECT titleRect = GdiDrawing::ToRect(panelLayout.tab);
        titleRect.left += 8;
        titleRect.right -= 8;
        GdiDrawing::DrawTabText(dc, titleRect, panel->title.c_str(), GdiDrawing::ToColorRef(panelLayout.active ? theme.textPrimary : theme.textSecondary));
    }
}

} // namespace kb::editor

#endif
