#include "rendering/DockPanelChromeRenderer.hpp"

#if defined(_WIN32)
#include "rendering/EditorSurfacePainter.hpp"
#include "rendering/EditorTabIndicatorPainter.hpp"
#include "rendering/GdiDrawing.hpp"

#include <algorithm>

namespace kb::editor {

void DockPanelChromeRenderer::Paint(HDC dc, const RECT& rect, const DockPanel& panel, const EditorTheme& theme, const EditorMetrics& metrics, bool active) const {
    const EditorSurfaceKind panelSurface = panel.kind == DockPanelKind::Scene ? EditorSurfaceKind::ScenePanel : EditorSurfaceKind::DockPanel;
    EditorSurfacePainter::Frame(dc, rect, theme, panelSurface, GdiDrawing::ToColorRef(theme.borderPanel));

    RECT tabStrip{ rect.left + 1, rect.top + 1, rect.right - 1, rect.top + metrics.tabStripHeight };
    EditorSurfacePainter::Fill(dc, tabStrip, theme, EditorSurfaceKind::HeaderStrip);

    RECT tab{ tabStrip.left, tabStrip.top, std::min(tabStrip.left + metrics.tabWidth, tabStrip.right), tabStrip.bottom };
    EditorSurfacePainter::Frame(
        dc,
        tab,
        theme,
        active ? EditorSurfaceKind::ActiveTab : EditorSurfaceKind::InactiveTab,
        active ? GdiDrawing::ToColorRef(theme.borderPanel) : GdiDrawing::ToColorRef(theme.strip));
    if (active) {
        EditorTabIndicatorPainter::PaintActive(dc, tab, theme);
    }

    RECT titleRect{ tab.left + 8, tab.top, tab.right - 8, tab.bottom };
    GdiDrawing::DrawTabText(dc, titleRect, panel.title.c_str(), GdiDrawing::ToColorRef(theme.textPrimary));
}

} // namespace kb::editor

#endif
