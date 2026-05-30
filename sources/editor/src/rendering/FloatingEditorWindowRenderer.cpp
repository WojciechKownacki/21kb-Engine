#include "rendering/FloatingEditorWindowRenderer.hpp"

#if defined(_WIN32)
#include "rendering/DockPanelChromeRenderer.hpp"
#include "rendering/FloatingWindowControlRenderer.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/PanelContentRenderer.hpp"

namespace kb::editor {

void FloatingEditorWindowRenderer::Paint(HDC dc, const RECT& client, const DockPanel& panel, const EditorTheme& theme, const EditorMetrics& metrics, const EditorSceneContext& sceneContext) const {
    ScopedFont titleFont(16, FW_SEMIBOLD);
    ScopedFont bodyFont(14, FW_NORMAL);
    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, titleFont.handle));

    GdiDrawing::DrawSharpFrame(dc, client, GdiDrawing::ToColorRef(theme.background), GdiDrawing::ToColorRef(theme.borderChrome));

    RECT panelRect = GdiDrawing::Inset(client, 1);
    DockPanelChromeRenderer{}.Paint(dc, panelRect, panel, theme, metrics, GdiDrawing::ToColorRef(panel.kind == DockPanelKind::Scene ? theme.chrome : theme.panel), true);

    FloatingWindowControlRenderer{}.Paint(dc, client, theme, metrics);

    SelectObject(dc, bodyFont.handle);
    RECT content = GdiDrawing::Inset(panelRect, metrics.panelPadding);
    content.top += metrics.tabStripHeight;
    PanelContentRenderer{}.Paint(dc, content, panelRect, panel, theme, metrics, sceneContext, true);

    SelectObject(dc, oldFont);
}

} // namespace kb::editor

#endif
