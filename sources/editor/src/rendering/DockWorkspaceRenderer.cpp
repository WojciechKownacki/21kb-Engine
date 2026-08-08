#include "rendering/DockWorkspaceRenderer.hpp"

#if defined(_WIN32)
#include "rendering/DockDropPreviewRenderer.hpp"
#include "rendering/DockWorkspaceChromeRenderer.hpp"
#include "rendering/DockWorkspaceContentRenderer.hpp"
#include "rendering/DockWorkspaceTabStripRenderer.hpp"
#include "rendering/EditorToolbarRenderer.hpp"
#include "rendering/GdiDrawing.hpp"

namespace kb::editor {

void DockWorkspaceRenderer::Paint(HWND parent, HDC dc, int width, int height, const EditorDockModel& dockModel, const EditorTheme& theme, const EditorMetrics& metrics, EditorSceneContext& sceneContext, const EditorRenderBackendSettings& renderBackendSettings, const DockDropPreview* preview, const DockPointerDrag* dockDrag, const EditorPlayModeState& playMode, const EditorShellInteractionState& shellInteraction, EditorSceneBgfxViewport* sceneViewport) const {
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
    toolbarRenderer.PaintToolbar(dc, GdiDrawing::ToRect(layout.toolbar), theme, sceneContext, playMode, shellInteraction);

    DockWorkspaceChromeRenderer{}.Paint(dc, layout, dockModel, theme);
    DockWorkspaceTabStripRenderer{}.Paint(parent, dc, layout, dockModel, dockDrag, theme);
    DockWorkspaceContentRenderer{}.Paint(parent, dc, layout, dockModel, theme, metrics, sceneContext, renderBackendSettings, sceneViewport);

    if (preview != nullptr) {
        DockDropPreviewRenderer{}.Paint(dc, *preview, theme);
    }

    toolbarRenderer.PaintMenu(dc, GdiDrawing::ToRect(layout.menu), theme, shellInteraction);
}

} // namespace kb::editor

#endif
