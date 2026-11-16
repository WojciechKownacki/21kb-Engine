#include "rendering/FloatingWindowBackBufferPainter.hpp"

#if defined(_WIN32)
#include "app/EditorWindowResizeInteraction.hpp"
#include "docking/DockWindowLayoutResolver.hpp"
#include "rendering/ConsoleDetailTextOverlay.hpp"
#include "rendering/DockTabTooltipOverlayWindow.hpp"
#include "rendering/DockWorkspaceTabStripRenderer.hpp"
#include "rendering/EditorSurfacePainter.hpp"
#include "rendering/FloatingEditorWindowRenderer.hpp"
#include "rendering/FloatingPanelGeometry.hpp"
#include "rendering/GdiBackBufferRenderer.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/InspectorAddComponentOverlayWindow.hpp"
#include "rendering/InspectorPanelRenderer.hpp"
#include "rendering/MaterialEditorPanelRenderer.hpp"
#include "rendering/MaterialPreviewViewportKeys.hpp"
#include "rendering/SceneViewportToolbarDropdownOverlayWindow.hpp"
#include "rendering/SceneViewportToolbarRenderer.hpp"
#include "rendering/SkeletalMeshEditorPanelLayout.hpp"
#include "scene/EditorTerrainService.hpp"

#include <string>
#include <vector>

namespace kb::editor {
namespace {

struct FloatingWindowPaintContext {
    HWND window = nullptr;
    const DockPanel* panel = nullptr;
    const EditorDockModel* dockModel = nullptr;
    const DockPointerDrag* dockDrag = nullptr;
    const EditorTheme* theme = nullptr;
    const EditorMetrics* metrics = nullptr;
    EditorSceneContext* sceneContext = nullptr;
    const EditorRenderBackendSettings* renderBackendSettings = nullptr;
    EditorSceneBgfxViewport* sceneViewport = nullptr;
};

[[nodiscard]] RECT FloatingPanelContentRect(const RECT& client, const EditorMetrics& metrics) noexcept {
    return FloatingPanelGeometry::Content(client, metrics.tabStripHeight);
}

void PaintBackBuffer(const GdiBackBufferPaintContext& paint, void* context) {
    auto* paintContext = static_cast<FloatingWindowPaintContext*>(context);
    const RECT content = FloatingPanelContentRect(paint.client, *paintContext->metrics);

    std::vector<EditorSceneBgfxViewport::HostSurfaceLayout> layouts;
    if (paintContext->panel->kind == DockPanelKind::Scene) {
        layouts.push_back(EditorSceneBgfxViewport::HostSurfaceLayout{
            .viewportKey = paintContext->panel->id,
            .bounds = SceneViewportToolbarRenderer::Resolve(
                content,
                paintContext->sceneContext->ViewportPreview(paintContext->panel->id),
                *paintContext->sceneContext).renderArea,
        });
    } else if (paintContext->panel->kind == DockPanelKind::Inspector) {
        if (const std::optional<RECT> preview = InspectorPanelRenderer::MaterialPreviewRect(content, *paintContext->sceneContext)) {
            layouts.push_back(EditorSceneBgfxViewport::HostSurfaceLayout{
                .viewportKey = kInspectorMaterialPreviewViewportKey,
                .bounds = *preview,
            });
        }
    } else if (paintContext->panel->kind == DockPanelKind::MaterialEditor) {
        if (const std::optional<RECT> preview = MaterialEditorPanelRenderer::MaterialPreviewRect(content, *paintContext->sceneContext)) {
            layouts.push_back(EditorSceneBgfxViewport::HostSurfaceLayout{
                .viewportKey = kMaterialEditorPreviewViewportKey,
                .bounds = *preview,
            });
        }
    } else if (paintContext->panel->kind == DockPanelKind::SkeletalMeshEditor &&
        paintContext->sceneContext->HasSkeletalMeshEditorAsset()) {
        layouts.push_back(EditorSceneBgfxViewport::HostSurfaceLayout{
            .viewportKey = paintContext->panel->id,
            .bounds = SkeletalMeshEditorPanelLayoutResolver::Resolve(
                content,
                paintContext->sceneContext->SkeletalMeshEditorToolboxWidth(),
                paintContext->sceneContext->SkeletalMeshEditorSkeletonTreeWidth(),
                paintContext->sceneContext->SkeletalMeshEditorSkeletonTreeHeight()).viewport,
        });
    }
    const std::span<const EditorSceneBgfxViewport::HostSurfaceLayout> layoutSpan{layouts.data(), layouts.size()};
    if (EditorWindowResizeInteraction::IsWindowResizing(paintContext->window)) {
        paintContext->sceneViewport->SyncHostSurfaceLayoutsForResize(paintContext->window, layoutSpan);
    } else {
        paintContext->sceneViewport->SyncHostSurfaceLayouts(paintContext->window, layoutSpan);
    }

    EditorSurfacePainter::Fill(paint.dc, paint.client, *paintContext->theme, EditorSurfaceKind::AppBackground);
    SetBkMode(paint.dc, TRANSPARENT);
    FloatingEditorWindowRenderer{}.Paint(paint.dc, paintContext->window, paint.client, *paintContext->panel, *paintContext->dockModel, paintContext->dockDrag, *paintContext->theme, *paintContext->metrics, *paintContext->sceneContext, *paintContext->renderBackendSettings, nullptr);
}

[[nodiscard]] SceneViewportToolbarDropdownOverlayWindow& FloatingSceneToolbarDropdownOverlay() {
    static SceneViewportToolbarDropdownOverlayWindow overlay;
    return overlay;
}

[[nodiscard]] InspectorAddComponentOverlayWindow& FloatingAddComponentOverlay() {
    static InspectorAddComponentOverlayWindow overlay;
    return overlay;
}

[[nodiscard]] DockTabTooltipOverlayWindow& FloatingTabTooltipOverlay() {
    static DockTabTooltipOverlayWindow overlay;
    return overlay;
}

// The window the tooltip is shown for, so that a window with no tab to explain does not put away another one's.
HWND g_tabTooltipWindow = nullptr;

// The pointer rests on a tab of this window whose title is cut short: its whole title.
void UpdateTabTooltip(HWND window, const DockPanel& panel, const EditorDockModel& dockModel, std::uint32_t hoveredTab, const EditorTheme& theme, const EditorMetrics& metrics) {
    bool shown = false;
    if (hoveredTab != 0U) {
        RECT client{};
        GetClientRect(window, &client);
        const DockLayout layout = DockWindowLayoutResolver::ResolveFloating(client, dockModel.Queries().FloatingGroupOfPanel(panel.id), dockModel, metrics);
        for (const DockPanelLayout& panelLayout : layout.panels) {
            if (panelLayout.panelId != hoveredTab) {
                continue;
            }
            const DockPanel* tabPanel = dockModel.Queries().FindPanel(panelLayout.panelId);
            const std::string title = tabPanel != nullptr ? DockWorkspaceTabStripRenderer::CutShortTitle(panelLayout, *tabPanel) : std::string{};
            if (!title.empty()) {
                FloatingTabTooltipOverlay().Show(window, GdiDrawing::ToRect(panelLayout.tab), title, theme);
                g_tabTooltipWindow = window;
                shown = true;
            }
            break;
        }
    }
    if (!shown && g_tabTooltipWindow == window) {
        FloatingTabTooltipOverlay().Hide();
        g_tabTooltipWindow = nullptr;
    }
}

} // namespace

void FloatingWindowBackBufferPainter::HideAllOverlays() noexcept {
    FloatingSceneToolbarDropdownOverlay().Hide();
    FloatingAddComponentOverlay().Hide();
    FloatingTabTooltipOverlay().Hide();
    g_tabTooltipWindow = nullptr;
}

void FloatingWindowBackBufferPainter::Paint(HWND window, const DockPanel& panel, const EditorDockModel& dockModel, const DockPointerDrag* dockDrag, std::uint32_t hoveredTab, const EditorTheme& theme, const EditorMetrics& metrics, EditorSceneContext& sceneContext, const EditorRenderBackendSettings& renderBackendSettings, EditorSceneBgfxViewport& sceneViewport) {
    FloatingWindowPaintContext context{
        .window = window,
        .panel = &panel,
        .dockModel = &dockModel,
        .dockDrag = dockDrag,
        .theme = &theme,
        .metrics = &metrics,
        .sceneContext = &sceneContext,
        .renderBackendSettings = &renderBackendSettings,
        .sceneViewport = &sceneViewport,
    };
    GdiBackBufferRenderer::Paint(window, &PaintBackBuffer, &context);
    UpdateTabTooltip(window, panel, dockModel, hoveredTab, theme, metrics);
    const EditorTerrainToolState& terrainTool = EditorTerrainService::ToolState();
    if (panel.kind == DockPanelKind::Scene &&
        (sceneContext.ViewportPreview(panel.id).ToolbarDropdown() != EditorViewportToolbarDropdown::None ||
         terrainTool.brushMenuOpen || terrainTool.brushShapeMenuOpen)) {
        RECT content{};
        GetClientRect(window, &content);
        content = FloatingPanelContentRect(content, metrics);
        FloatingSceneToolbarDropdownOverlay().Show(window, content, panel.id, theme, sceneContext);
    } else {
        FloatingSceneToolbarDropdownOverlay().Hide();
    }
    if (panel.kind == DockPanelKind::Inspector && sceneContext.Inspector().IsAddComponentBrowserOpen()) {
        RECT content{};
        GetClientRect(window, &content);
        content = FloatingPanelContentRect(content, metrics);
        FloatingAddComponentOverlay().Show(window, content, theme, sceneContext);
    } else {
        FloatingAddComponentOverlay().HideForOwner(window);
    }
    if (panel.kind != DockPanelKind::Console) {
        ConsoleDetailTextOverlay::Hide(window);
    }
}

} // namespace kb::editor

#endif
