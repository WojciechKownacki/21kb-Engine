#include "rendering/PanelContentRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/HierarchyPanelRenderer.hpp"
#include "rendering/InspectorPanelRenderer.hpp"

namespace kb::editor {

void PanelContentRenderer::Paint(HDC dc, const RECT& content, const RECT& panelFrame, const DockPanel& panel, const EditorTheme& theme, const EditorMetrics& metrics, const EditorSceneContext& sceneContext, bool floating) const {
    switch (panel.kind) {
    case DockPanelKind::Hierarchy:
        HierarchyPanelRenderer{}.Paint(dc, content, theme, sceneContext);
        break;
    case DockPanelKind::Inspector:
        InspectorPanelRenderer{}.Paint(dc, content, theme, sceneContext);
        break;
    case DockPanelKind::Assets:
        GdiDrawing::DrawTextBlock(dc, content, "Scenes\nMaterials\nMeshes\nTextures", GdiDrawing::ToColorRef(theme.textSecondary));
        break;
    case DockPanelKind::Console:
        GdiDrawing::DrawTextBlock(
            dc,
            content,
            floating ? "[layout] Floating console host\n[info] Awaiting editor events" : "[info] Native window initialized\n[layout] Dock model ready\n[layout] Floating host ready",
            GdiDrawing::ToColorRef(theme.textDisabled));
        break;
    case DockPanelKind::Scene:
        PaintSceneGrid(dc, panelFrame, theme, metrics);
        break;
    case DockPanelKind::Generic:
    default:
        break;
    }
}

void PanelContentRenderer::PaintSceneGrid(HDC dc, RECT scene, const EditorTheme& theme, const EditorMetrics& metrics) const {
    RECT sceneInner = GdiDrawing::Inset(scene, 20);
    sceneInner.top += metrics.tabStripHeight + 12;

    ScopedPen gridPen(1, GdiDrawing::ToColorRef(theme.gridLine));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, gridPen.handle));

    for (int x = sceneInner.left; x < sceneInner.right; x += 32) {
        MoveToEx(dc, x, sceneInner.top, nullptr);
        LineTo(dc, x, sceneInner.bottom);
    }

    for (int y = sceneInner.top; y < sceneInner.bottom; y += 32) {
        MoveToEx(dc, sceneInner.left, y, nullptr);
        LineTo(dc, sceneInner.right, y);
    }

    ScopedPen accentPen(2, GdiDrawing::ToColorRef(theme.accent));
    SelectObject(dc, accentPen.handle);

    const int centerX = (sceneInner.left + sceneInner.right) / 2;
    const int centerY = (sceneInner.top + sceneInner.bottom) / 2;
    Ellipse(dc, centerX - 48, centerY - 48, centerX + 48, centerY + 48);

    SelectObject(dc, oldPen);
}

} // namespace kb::editor

#endif
