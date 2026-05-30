#include "rendering/DockWorkspaceContentRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/PanelContentRenderer.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

namespace kb::editor {

void DockWorkspaceContentRenderer::Paint(HDC dc, const DockLayout& layout, const EditorDockModel& dockModel, const EditorTheme& theme, const EditorMetrics& metrics, const EditorSceneContext& sceneContext) const {
    ScopedFont bodyFont(14, FW_NORMAL);
    const ScopedGdiObject selectedFont(dc, bodyFont.handle);

    PanelContentRenderer contentRenderer;
    for (const DockPanelLayout& panelLayout : layout.panels) {
        const DockPanel* panel = dockModel.Queries().FindPanel(panelLayout.panelId);
        if (panel == nullptr || !panelLayout.active) {
            continue;
        }

        const RECT content = GdiDrawing::ToRect(panelLayout.content);
        contentRenderer.Paint(dc, content, content, *panel, theme, metrics, sceneContext, false);
    }
}

} // namespace kb::editor

#endif
