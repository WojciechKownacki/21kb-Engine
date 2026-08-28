#include "rendering/DockWorkspaceContentRenderer.hpp"

#if defined(_WIN32)
#include "rendering/ConsoleDetailTextOverlay.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/PanelContentRenderer.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

namespace kb::editor {

void DockWorkspaceContentRenderer::Paint(HWND parent, HDC dc, const RECT& dirty, const DockLayout& layout, const EditorDockModel& dockModel, const EditorTheme& theme, const EditorMetrics& metrics, EditorSceneContext& sceneContext, const EditorRenderBackendSettings& renderBackendSettings, EditorSceneBgfxViewport* sceneViewport) const {
    ScopedFont bodyFont(14, FW_NORMAL);
    const ScopedGdiObject selectedFont(dc, bodyFont.handle);
    const RECT workspace = GdiDrawing::ToRect(layout.workspace);
    bool paintedConsole = false;

    PanelContentRenderer contentRenderer;
    for (const DockPanelLayout& panelLayout : layout.panels) {
        const DockPanel* panel = dockModel.Queries().FindPanel(panelLayout.panelId);
        if (panel == nullptr || !panelLayout.active) {
            continue;
        }

        paintedConsole = paintedConsole || panel->kind == DockPanelKind::Console;

        const RECT content = GdiDrawing::ToRect(panelLayout.content);
        const RECT panelFrame = GdiDrawing::ToRect(panelLayout.frame);
        const RECT contentClip = GdiDrawing::ToRect(panelLayout.contentClip);
        // Console and Script Editor position their own child windows from inside their
        // paint, so they must run even when their pixels are untouched.
        const bool ownsChildSurface = panel->kind == DockPanelKind::Console ||
            panel->kind == DockPanelKind::ScriptEditor;
        RECT intersection{};
        if (!ownsChildSurface && IntersectRect(&intersection, &panelFrame, &dirty) == 0) {
            continue;
        }
        contentRenderer.Paint(dc, content, panelFrame, contentClip, workspace, *panel, theme, metrics, sceneContext, renderBackendSettings, false, sceneViewport, parent);
    }
    if (!paintedConsole) {
        ConsoleDetailTextOverlay::Hide(parent);
    }
}

} // namespace kb::editor

#endif
