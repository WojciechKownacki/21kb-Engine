#include "rendering/PanelContentRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/HierarchyPanelRenderer.hpp"
#include "rendering/InspectorPanelRenderer.hpp"
#include "rendering/ProjectFilesPanelRenderer.hpp"
#include "rendering/ScenePanelContentRenderer.hpp"

namespace kb::editor {

void PanelContentRenderer::Paint(
    HDC dc,
    const RECT& content,
    const RECT& panelFrame,
    const RECT& contentClip,
    const RECT& overlayBounds,
    const DockPanel& panel,
    const EditorTheme& theme,
    const EditorMetrics& metrics,
    const EditorSceneContext& sceneContext,
    bool floating,
    EditorSceneBgfxViewport* sceneViewport,
    HWND sceneViewportHost) const {
    static_cast<void>(panelFrame);
    static_cast<void>(metrics);

    const int savedDc = SaveDC(dc);
    IntersectClipRect(dc, contentClip.left, contentClip.top, contentClip.right, contentClip.bottom);

    switch (panel.kind) {
    case DockPanelKind::Hierarchy:
        HierarchyPanelRenderer{}.Paint(dc, content, theme, sceneContext);
        break;
    case DockPanelKind::Inspector:
        InspectorPanelRenderer{}.Paint(dc, content, theme, sceneContext);
        break;
    case DockPanelKind::Assets:
        ProjectFilesPanelRenderer{}.Paint(dc, content, overlayBounds, theme, sceneContext);
        break;
    case DockPanelKind::Console:
        GdiDrawing::DrawTextBlock(
            dc,
            content,
            floating ? "[layout] Floating console host\n[info] Awaiting editor events" : "[info] Native window initialized\n[layout] Dock model ready\n[layout] Floating host ready",
            GdiDrawing::ToColorRef(theme.textDisabled));
        break;
    case DockPanelKind::Scene:
        ScenePanelContentRenderer{}.Paint(dc, content, panel, theme, sceneContext, sceneViewport, sceneViewportHost);
        break;
    case DockPanelKind::Generic:
    default:
        break;
    }

    RestoreDC(dc, savedDc);
}

} // namespace kb::editor

#endif
