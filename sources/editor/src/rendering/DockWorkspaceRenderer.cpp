#include "rendering/DockWorkspaceRenderer.hpp"

#if defined(_WIN32)
#include "rendering/DockDropPreviewRenderer.hpp"
#include "rendering/DockWorkspaceChromeRenderer.hpp"
#include "rendering/DockWorkspaceContentRenderer.hpp"
#include "rendering/DockWorkspaceTabStripRenderer.hpp"
#include "rendering/EditorToolbarRenderer.hpp"
#include "rendering/GdiDrawing.hpp"

namespace kb::editor {

void DockWorkspaceRenderer::Paint(HDC dc, int width, int height, const EditorDockModel& dockModel, const EditorTheme& theme, const EditorMetrics& metrics, const EditorSceneContext& sceneContext, const DockDropPreview* preview, EditorSceneBgfxViewport& sceneViewport) const {
    const DockLayout layout = dockModel.Queries().BuildLayout(
        width,
        height,
        metrics.menuHeight,
        metrics.toolbarHeight,
        metrics.tabStripHeight,
        metrics.tabMinWidth,
        metrics.tabWidth,
        metrics.splitterSize,
        metrics.panelPadding);

    EditorToolbarRenderer toolbarRenderer;
    toolbarRenderer.PaintMenu(dc, GdiDrawing::ToRect(layout.menu), theme);
    toolbarRenderer.PaintToolbar(dc, GdiDrawing::ToRect(layout.toolbar), theme);

    DockWorkspaceChromeRenderer{}.Paint(dc, layout, dockModel, theme);
    DockWorkspaceTabStripRenderer{}.Paint(dc, layout, dockModel, theme);
    DockWorkspaceContentRenderer{}.Paint(dc, layout, dockModel, theme, metrics, sceneContext, sceneViewport);

    if (preview != nullptr) {
        DockDropPreviewRenderer{}.Paint(dc, *preview, theme);
    }
}

} // namespace kb::editor

#endif
