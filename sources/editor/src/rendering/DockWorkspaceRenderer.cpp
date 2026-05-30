#include "rendering/DockWorkspaceRenderer.hpp"

#if defined(_WIN32)
#include "rendering/DockDropPreviewRenderer.hpp"
#include "rendering/EditorToolbarRenderer.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/PanelContentRenderer.hpp"

namespace kb::editor {

void DockWorkspaceRenderer::Paint(HDC dc, int width, int height, const EditorDockModel& dockModel, const EditorTheme& theme, const EditorMetrics& metrics, const EditorSceneContext& sceneContext, const DockDropPreview* preview) const {
    const DockLayout layout = dockModel.BuildLayout(
        width,
        height,
        metrics.menuHeight,
        metrics.toolbarHeight,
        metrics.tabStripHeight,
        metrics.tabMinWidth,
        metrics.tabWidth,
        metrics.splitterSize,
        metrics.panelPadding);

    EditorToolbarRenderer toolbarRenderer;
    toolbarRenderer.PaintMenu(dc, GdiDrawing::ToRect(layout.menu), theme);
    toolbarRenderer.PaintToolbar(dc, GdiDrawing::ToRect(layout.toolbar), theme);

    PaintSplitters(dc, layout, theme);

    ScopedFont titleFont(16, FW_SEMIBOLD);
    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, titleFont.handle));
    PaintLeaves(dc, layout, dockModel, theme);
    PaintTabs(dc, layout, dockModel, theme);

    ScopedFont bodyFont(14, FW_NORMAL);
    SelectObject(dc, bodyFont.handle);
    PaintActivePanelContent(dc, layout, dockModel, theme, metrics, sceneContext);
    SelectObject(dc, oldFont);

    if (preview != nullptr) {
        DockDropPreviewRenderer{}.Paint(dc, *preview, theme);
    }
}

void DockWorkspaceRenderer::PaintSplitters(HDC dc, const DockLayout& layout, const EditorTheme& theme) {
    for (const DockSplitterLayout& splitter : layout.splitters) {
        GdiDrawing::FillRectColor(dc, GdiDrawing::ToRect(splitter.rect), GdiDrawing::ToColorRef(theme.splitter));
    }
}

void DockWorkspaceRenderer::PaintLeaves(HDC dc, const DockLayout& layout, const EditorDockModel& dockModel, const EditorTheme& theme) {
    for (const DockLeafLayout& leaf : layout.leaves) {
        const DockPanel* activePanel = dockModel.FindPanel(leaf.activePanelId);
        if (activePanel == nullptr) {
            continue;
        }

        const bool scene = activePanel->kind == DockPanelKind::Scene;
        GdiDrawing::DrawSharpFrame(dc, GdiDrawing::ToRect(leaf.frame), scene ? GdiDrawing::ToColorRef(theme.chrome) : GdiDrawing::ToColorRef(theme.panel), GdiDrawing::ToColorRef(theme.borderPanel));
        GdiDrawing::FillRectColor(dc, GdiDrawing::ToRect(leaf.tabStrip), GdiDrawing::ToColorRef(theme.strip));
    }
}

void DockWorkspaceRenderer::PaintTabs(HDC dc, const DockLayout& layout, const EditorDockModel& dockModel, const EditorTheme& theme) {
    for (const DockPanelLayout& panelLayout : layout.panels) {
        const DockPanel* panel = dockModel.FindPanel(panelLayout.panelId);
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
}

void DockWorkspaceRenderer::PaintActivePanelContent(HDC dc, const DockLayout& layout, const EditorDockModel& dockModel, const EditorTheme& theme, const EditorMetrics& metrics, const EditorSceneContext& sceneContext) {
    PanelContentRenderer contentRenderer;
    for (const DockPanelLayout& panelLayout : layout.panels) {
        const DockPanel* panel = dockModel.FindPanel(panelLayout.panelId);
        if (panel == nullptr || !panelLayout.active) {
            continue;
        }

        const RECT content = GdiDrawing::ToRect(panelLayout.content);
        contentRenderer.Paint(dc, content, content, *panel, theme, metrics, sceneContext, false);
    }
}

} // namespace kb::editor

#endif
