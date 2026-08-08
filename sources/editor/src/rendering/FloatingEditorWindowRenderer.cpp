#include "rendering/FloatingEditorWindowRenderer.hpp"

#if defined(_WIN32)
#include "rendering/DockPanelChromeRenderer.hpp"
#include "rendering/EditorSurfacePainter.hpp"
#include "rendering/FloatingWindowControlRenderer.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/PanelContentRenderer.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

namespace kb::editor {

void FloatingEditorWindowRenderer::Paint(HDC dc, HWND window, const RECT& client, const DockPanel& panel, const EditorTheme& theme, const EditorMetrics& metrics, EditorSceneContext& sceneContext, const EditorRenderBackendSettings& renderBackendSettings, EditorSceneBgfxViewport* sceneViewport) const {
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
    const bool viewportPanel = panel.kind == DockPanelKind::Scene;
    RECT content = viewportPanel ? panelRect : GdiDrawing::Inset(panelRect, metrics.panelPadding);
    content.top += metrics.tabStripHeight;
    PanelContentRenderer{}.Paint(dc, content, panelRect, content, client, panel, theme, metrics, sceneContext, renderBackendSettings, true, sceneViewport, window);
}

} // namespace kb::editor

#endif
