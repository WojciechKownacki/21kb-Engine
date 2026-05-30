#include "rendering/PanelContentRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/HierarchyPanelRenderer.hpp"
#include "rendering/InspectorPanelRenderer.hpp"
#include "rendering/SceneGridRenderer.hpp"

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
        SceneGridRenderer{}.Paint(dc, panelFrame, theme, metrics);
        break;
    case DockPanelKind::Generic:
    default:
        break;
    }
}

} // namespace kb::editor

#endif
