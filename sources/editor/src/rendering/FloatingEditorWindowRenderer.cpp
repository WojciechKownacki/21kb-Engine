#include "rendering/FloatingEditorWindowRenderer.hpp"

#if defined(_WIN32)
#include "docking/DockWindowLayoutResolver.hpp"
#include "rendering/DockWorkspaceChromeRenderer.hpp"
#include "rendering/DockWorkspaceTabStripRenderer.hpp"
#include "rendering/EditorSurfacePainter.hpp"
#include "rendering/FloatingPanelGeometry.hpp"
#include "rendering/FloatingWindowControlRenderer.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/PanelContentRenderer.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

namespace kb::editor {

void FloatingEditorWindowRenderer::Paint(HDC dc, HWND window, const RECT& client, const DockPanel& panel, const EditorDockModel& dockModel, const DockPointerDrag* dockDrag, const EditorTheme& theme, const EditorMetrics& metrics, EditorSceneContext& sceneContext, const EditorRenderBackendSettings& renderBackendSettings, EditorSceneBgfxViewport* sceneViewport) const {
    ScopedFont bodyFont(14, FW_NORMAL);

    EditorSurfacePainter::Frame(dc, client, theme, EditorSurfaceKind::AppBackground, GdiDrawing::ToColorRef(theme.borderChrome));

    RECT panelRect = GdiDrawing::Inset(client, 1);
    // The tabs of the window are drawn as the tabs of a docked group are.
    const DockLayout layout = DockWindowLayoutResolver::ResolveFloating(client, dockModel.Queries().FloatingGroupOfPanel(panel.id), dockModel, metrics);
    DockWorkspaceChromeRenderer{}.Paint(dc, layout, dockModel, theme);
    DockWorkspaceTabStripRenderer{}.Paint(window, dc, layout, dockModel, dockDrag, theme);

    FloatingWindowControlRenderer{}.Paint(dc, client, theme, metrics);

    const ScopedGdiObject selectedBodyFont(dc, bodyFont.handle);
    const RECT content = FloatingPanelGeometry::Content(client, metrics.tabStripHeight);
    PanelContentRenderer{}.Paint(dc, content, panelRect, content, client, panel, theme, metrics, sceneContext, renderBackendSettings, true, sceneViewport, window);
}

} // namespace kb::editor

#endif
