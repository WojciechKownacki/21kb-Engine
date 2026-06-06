#include "rendering/DockWorkspaceContentRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/PanelContentRenderer.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

namespace kb::editor {

void DockWorkspaceContentRenderer::Paint(HDC dc, const DockLayout& layout, const EditorDockModel& dockModel, const EditorTheme& theme, const EditorMetrics& metrics, const EditorSceneContext& sceneContext, EditorSceneBgfxViewport* sceneViewport) const {
    ScopedFont bodyFont(14, FW_NORMAL);
    const ScopedGdiObject selectedFont(dc, bodyFont.handle);
    const RECT workspace = GdiDrawing::ToRect(layout.workspace);

    PanelContentRenderer contentRenderer;
    for (const DockPanelLayout& panelLayout : layout.panels) {
        const DockPanel* panel = dockModel.Queries().FindPanel(panelLayout.panelId);
        if (panel == nullptr || !panelLayout.active) {
            continue;
        }

        const RECT content = GdiDrawing::ToRect(panelLayout.content);
        const RECT panelFrame = GdiDrawing::ToRect(panelLayout.frame);
        const RECT contentClip = GdiDrawing::ToRect(panelLayout.contentClip);
        contentRenderer.Paint(dc, content, panelFrame, contentClip, workspace, *panel, theme, metrics, sceneContext, false, sceneViewport);
    }
}

} // namespace kb::editor

#endif
