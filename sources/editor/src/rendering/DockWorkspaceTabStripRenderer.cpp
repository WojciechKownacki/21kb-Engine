#include "rendering/DockWorkspaceTabStripRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"

namespace kb::editor {

void DockWorkspaceTabStripRenderer::Paint(HDC dc, const DockLayout& layout, const EditorDockModel& dockModel, const EditorTheme& theme) const {
    ScopedFont titleFont(16, FW_SEMIBOLD);
    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, titleFont.handle));

    for (const DockPanelLayout& panelLayout : layout.panels) {
        const DockPanel* panel = dockModel.Queries().FindPanel(panelLayout.panelId);
        if (panel == nullptr) {
            continue;
        }

        GdiDrawing::DrawSharpFrame(
            dc,
            GdiDrawing::ToRect(panelLayout.tab),
            panelLayout.active ? GdiDrawing::ToColorRef(theme.tabActive) : GdiDrawing::ToColorRef(theme.tabInactive),
            panelLayout.active ? GdiDrawing::ToColorRef(theme.borderPanel) : GdiDrawing::ToColorRef(theme.strip));

        RECT titleRect = GdiDrawing::ToRect(panelLayout.tab);
        titleRect.left += 8;
        titleRect.right -= 8;
        GdiDrawing::DrawTabText(dc, titleRect, panel->title.c_str(), GdiDrawing::ToColorRef(panelLayout.active ? theme.textPrimary : theme.textSecondary));
    }

    SelectObject(dc, oldFont);
}

} // namespace kb::editor

#endif
