#include "rendering/FloatingEditorWindowRenderer.hpp"

#if defined(_WIN32)
#include "rendering/DockPanelChromeRenderer.hpp"
#include "rendering/EditorSurfacePainter.hpp"
#include "rendering/FloatingWindowControlRenderer.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/PanelContentRenderer.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

namespace kb::editor {

void FloatingEditorWindowRenderer::Paint(HDC dc, const RECT& client, const DockPanel& panel, const EditorTheme& theme, const EditorMetrics& metrics, const EditorSceneContext& sceneContext) const {
    ScopedFont titleFont(16, FW_SEMIBOLD);
    ScopedFont bodyFont(14, FW_NORMAL);

    EditorSurfacePainter::Frame(dc, client, theme, EditorSurfaceKind::AppBackground, GdiDrawing::ToColorRef(theme.borderChrome));

    RECT panelRect = GdiDrawing::Inset(client, 1);
    {
        const ScopedGdiObject selectedTitleFont(dc, titleFont.handle);
        DockPanelChromeRenderer{}.Paint(dc, panelRect, panel, theme, metrics, true);
    }

    FloatingWindowControlRenderer{}.Paint(dc, client, theme, metrics);

    const ScopedGdiObject selectedBodyFont(dc, bodyFont.handle);
    RECT content = GdiDrawing::Inset(panelRect, metrics.panelPadding);
    content.top += metrics.tabStripHeight;
    PanelContentRenderer{}.Paint(dc, content, panelRect, panel, theme, metrics, sceneContext, true);
}

} // namespace kb::editor

#endif
