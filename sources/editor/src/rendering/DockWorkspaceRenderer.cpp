#include "rendering/DockWorkspaceRenderer.hpp"

#if defined(_WIN32)
#include "rendering/DockDropPreviewRenderer.hpp"
#include "rendering/DockWorkspaceChromeRenderer.hpp"
#include "rendering/DockWorkspaceContentRenderer.hpp"
#include "rendering/DockWorkspaceTabStripRenderer.hpp"
#include "rendering/EditorToolbarRenderer.hpp"
#include "rendering/GdiDrawing.hpp"

namespace kb::editor {

void DockWorkspaceRenderer::Paint(HWND parent, HDC dc, const RECT& dirty, int width, int height, const EditorDockModel& dockModel, const EditorTheme& theme, const EditorMetrics& metrics, EditorSceneContext& sceneContext, const EditorRenderBackendSettings& renderBackendSettings, const DockDropPreview* preview, const DockPointerDrag* dockDrag, const EditorPlayModeState& playMode, const EditorShellInteractionState& shellInteraction, EditorSceneBgfxViewport* sceneViewport, std::string_view graphicsApiLabel) const {
    const DockLayout layout = dockModel.Queries().BuildLayout(
        width,
        height,
        metrics.menuHeight, metrics.statusBarHeight,
        metrics.tabStripHeight,
        metrics.tabMinWidth,
        metrics.tabWidth,
        metrics.splitterSize);

    EditorToolbarRenderer toolbarRenderer;
    toolbarRenderer.PaintToolbar(dc, GdiDrawing::ToRect(layout.toolbar), theme, sceneContext, playMode, shellInteraction);

    DockWorkspaceChromeRenderer{}.Paint(dc, layout, dockModel, theme);
    DockWorkspaceTabStripRenderer{}.Paint(parent, dc, layout, dockModel, dockDrag, theme);
    DockWorkspaceContentRenderer{}.Paint(parent, dc, dirty, layout, dockModel, theme, metrics, sceneContext, renderBackendSettings, sceneViewport);

    // While the game runs, the Scene View is framed in the transport's colour - green playing,
    // amber paused - on the panel's own border, outside the picture, so nothing the game draws
    // (a health bar in a corner) is covered and edits are not made by mistake.
    if (playMode.IsPlaying() || playMode.IsPaused()) {
        const COLORREF colour = playMode.IsPaused() ? RGB(224, 165, 54) : RGB(63, 194, 123);
        for (const DockPanelLayout& panelLayout : layout.panels) {
            const DockPanel* panel = dockModel.Queries().FindPanel(panelLayout.panelId);
            if (panel == nullptr || panel->kind != DockPanelKind::Scene || !panelLayout.active) {
                continue;
            }
            const RECT content = GdiDrawing::ToRect(panelLayout.content);
            const RECT ring{ content.left - 2, content.top - 1, content.right + 2, content.bottom + 2 };
            GdiDrawing::FillRectColor(dc, RECT{ ring.left, ring.top, ring.right, content.top }, colour);
            GdiDrawing::FillRectColor(dc, RECT{ ring.left, content.bottom, ring.right, ring.bottom }, colour);
            GdiDrawing::FillRectColor(dc, RECT{ ring.left, content.top, content.left, content.bottom }, colour);
            GdiDrawing::FillRectColor(dc, RECT{ content.right, content.top, ring.right, content.bottom }, colour);
        }
    }

    if (preview != nullptr) {
        DockDropPreviewRenderer{}.Paint(dc, *preview, theme);
    }

    toolbarRenderer.PaintMenu(dc, GdiDrawing::ToRect(layout.menu), theme, shellInteraction);
    toolbarRenderer.PaintStatusBar(dc, GdiDrawing::ToRect(layout.statusBar), theme, sceneContext, playMode, graphicsApiLabel,
        dockModel.Queries().DrawerCollapsed());
}

} // namespace kb::editor

#endif
