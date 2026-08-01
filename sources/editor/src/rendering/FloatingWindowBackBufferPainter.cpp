#include "rendering/FloatingWindowBackBufferPainter.hpp"

#if defined(_WIN32)
#include "app/EditorWindowResizeInteraction.hpp"
#include "rendering/ConsoleDetailTextOverlay.hpp"
#include "rendering/EditorSurfacePainter.hpp"
#include "rendering/FloatingEditorWindowRenderer.hpp"
#include "rendering/GdiBackBufferRenderer.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/InspectorAddComponentOverlayWindow.hpp"
#include "rendering/InspectorPanelRenderer.hpp"
#include "rendering/MaterialEditorPanelRenderer.hpp"
#include "rendering/MaterialPreviewViewportKeys.hpp"
#include "rendering/SceneViewportToolbarDropdownOverlayWindow.hpp"
#include "rendering/SceneViewportToolbarRenderer.hpp"

#include <vector>

namespace kb::editor {
namespace {

struct FloatingWindowPaintContext {
    HWND window = nullptr;
    const DockPanel* panel = nullptr;
    const EditorTheme* theme = nullptr;
    const EditorMetrics* metrics = nullptr;
    EditorSceneContext* sceneContext = nullptr;
    const EditorRenderBackendSettings* renderBackendSettings = nullptr;
    EditorSceneBgfxViewport* sceneViewport = nullptr;
};

[[nodiscard]] RECT FloatingPanelContentRect(const RECT& client, const DockPanel& panel, const EditorMetrics& metrics) noexcept {
    RECT panelRect = GdiDrawing::Inset(client, 1);
    RECT content = panel.kind == DockPanelKind::Scene
        ? panelRect
        : GdiDrawing::Inset(panelRect, metrics.panelPadding);
    content.top += metrics.tabStripHeight;
    return content;
}

void PaintBackBuffer(const GdiBackBufferPaintContext& paint, void* context) {
    auto* paintContext = static_cast<FloatingWindowPaintContext*>(context);
    const RECT content = FloatingPanelContentRect(paint.client, *paintContext->panel, *paintContext->metrics);

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
    }
    const std::span<const EditorSceneBgfxViewport::HostSurfaceLayout> layoutSpan{layouts.data(), layouts.size()};
    if (EditorWindowResizeInteraction::IsWindowResizing(paintContext->window)) {
        paintContext->sceneViewport->SyncHostSurfaceLayoutsForResize(paintContext->window, layoutSpan);
    } else {
        paintContext->sceneViewport->SyncHostSurfaceLayouts(paintContext->window, layoutSpan);
    }

    EditorSurfacePainter::Fill(paint.dc, paint.client, *paintContext->theme, EditorSurfaceKind::AppBackground);
    SetBkMode(paint.dc, TRANSPARENT);
    FloatingEditorWindowRenderer{}.Paint(paint.dc, paintContext->window, paint.client, *paintContext->panel, *paintContext->theme, *paintContext->metrics, *paintContext->sceneContext, *paintContext->renderBackendSettings, nullptr);
}

[[nodiscard]] SceneViewportToolbarDropdownOverlayWindow& FloatingSceneToolbarDropdownOverlay() {
    static SceneViewportToolbarDropdownOverlayWindow overlay;
    return overlay;
}

[[nodiscard]] InspectorAddComponentOverlayWindow& FloatingAddComponentOverlay() {
    static InspectorAddComponentOverlayWindow overlay;
    return overlay;
}

} // namespace

void FloatingWindowBackBufferPainter::Paint(HWND window, const DockPanel& panel, const EditorTheme& theme, const EditorMetrics& metrics, EditorSceneContext& sceneContext, const EditorRenderBackendSettings& renderBackendSettings, EditorSceneBgfxViewport& sceneViewport) {
    FloatingWindowPaintContext context{
        .window = window,
        .panel = &panel,
        .theme = &theme,
        .metrics = &metrics,
        .sceneContext = &sceneContext,
        .renderBackendSettings = &renderBackendSettings,
        .sceneViewport = &sceneViewport,
    };
    GdiBackBufferRenderer::Paint(window, &PaintBackBuffer, &context);
    if (panel.kind == DockPanelKind::Scene && sceneContext.ViewportPreview(panel.id).ToolbarDropdown() != EditorViewportToolbarDropdown::None) {
        RECT content{};
        GetClientRect(window, &content);
        content = FloatingPanelContentRect(content, panel, metrics);
        FloatingSceneToolbarDropdownOverlay().Show(window, content, panel.id, theme, sceneContext);
    } else {
        FloatingSceneToolbarDropdownOverlay().Hide();
    }
    if (panel.kind == DockPanelKind::Inspector && sceneContext.Inspector().IsAddComponentBrowserOpen()) {
        RECT content{};
        GetClientRect(window, &content);
        content = FloatingPanelContentRect(content, panel, metrics);
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
