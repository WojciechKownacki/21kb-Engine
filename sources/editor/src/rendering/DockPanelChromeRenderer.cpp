#include "rendering/DockPanelChromeRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"

#include <algorithm>

namespace kb::editor {

void DockPanelChromeRenderer::Paint(HDC dc, const RECT& rect, const DockPanel& panel, const EditorTheme& theme, const EditorMetrics& metrics, COLORREF fill, bool active) const {
    GdiDrawing::DrawSharpFrame(dc, rect, fill, GdiDrawing::ToColorRef(theme.borderPanel));

    RECT tabStrip{ rect.left + 1, rect.top + 1, rect.right - 1, rect.top + metrics.tabStripHeight };
    GdiDrawing::FillRectColor(dc, tabStrip, GdiDrawing::ToColorRef(theme.strip));

    RECT tab{ tabStrip.left, tabStrip.top, std::min(tabStrip.left + metrics.tabWidth, tabStrip.right), tabStrip.bottom };
    GdiDrawing::DrawSharpFrame(
        dc,
        tab,
        active ? GdiDrawing::ToColorRef(theme.tabActive) : GdiDrawing::ToColorRef(theme.tabInactive),
        active ? GdiDrawing::ToColorRef(theme.borderPanel) : GdiDrawing::ToColorRef(theme.strip));

    RECT titleRect{ tab.left + 8, tab.top, tab.right - 8, tab.bottom };
    GdiDrawing::DrawTabText(dc, titleRect, panel.title.c_str(), GdiDrawing::ToColorRef(theme.textPrimary));
}

} // namespace kb::editor

#endif
