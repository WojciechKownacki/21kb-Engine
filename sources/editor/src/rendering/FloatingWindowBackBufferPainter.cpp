#include "rendering/FloatingWindowBackBufferPainter.hpp"

#if defined(_WIN32)
#include "rendering/ConsoleDetailTextOverlay.hpp"
#include "rendering/EditorSurfacePainter.hpp"
#include "rendering/FloatingEditorWindowRenderer.hpp"
#include "rendering/GdiBackBufferRenderer.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/SceneViewportToolbarDropdownOverlayWindow.hpp"
#include "rendering/SceneViewportToolbarRenderer.hpp"

namespace kb::editor {
namespace {

struct FloatingWindowPaintContext {
    HWND window = nullptr;
    const DockPanel* panel = nullptr;
    const EditorTheme* theme = nullptr;
    const EditorMetrics* metrics = nullptr;
    const EditorSceneContext* sceneContext = nullptr;
    EditorSceneBgfxViewport* sceneViewport = nullptr;
};

void PaintBackBuffer(const GdiBackBufferPaintContext& paint, void* context) {
    auto* paintContext = static_cast<FloatingWindowPaintContext*>(context);
    const bool viewportPanel = paintContext->panel->kind == DockPanelKind::Scene;
    if (viewportPanel) {
        RECT content = paint.client;
        content.top += paintContext->metrics->floatingChromeHeight;
        const EditorSceneBgfxViewport::HostSurfaceLayout layout{
            .viewportKey = paintContext->panel->id,
            .bounds = SceneViewportToolbarRenderer::Resolve(content, paintContext->sceneContext->ViewportPreview(paintContext->panel->id)).renderArea,
        };
        paintContext->sceneViewport->SyncHostSurfaceLayouts(
            paintContext->window,
            std::span<const EditorSceneBgfxViewport::HostSurfaceLayout>{&layout, 1U});
    }

    const bool presentSceneViewport = viewportPanel && paintContext->sceneViewport->PresentRequested();
    if (presentSceneViewport) {
        paintContext->sceneViewport->BeginPaintLayout(paintContext->window);
    }

    EditorSurfacePainter::Fill(paint.dc, paint.client, *paintContext->theme, EditorSurfaceKind::AppBackground);
    SetBkMode(paint.dc, TRANSPARENT);
    FloatingEditorWindowRenderer{}.Paint(paint.dc, paintContext->window, paint.client, *paintContext->panel, *paintContext->theme, *paintContext->metrics, *paintContext->sceneContext, presentSceneViewport ? paintContext->sceneViewport : nullptr);

    if (presentSceneViewport) {
        paintContext->sceneViewport->EndPaintLayout();
    }
}

[[nodiscard]] SceneViewportToolbarDropdownOverlayWindow& FloatingSceneToolbarDropdownOverlay() {
    static SceneViewportToolbarDropdownOverlayWindow overlay;
    return overlay;
}

} // namespace

void FloatingWindowBackBufferPainter::Paint(HWND window, const DockPanel& panel, const EditorTheme& theme, const EditorMetrics& metrics, const EditorSceneContext& sceneContext, EditorSceneBgfxViewport& sceneViewport) {
    FloatingWindowPaintContext context{
        .window = window,
        .panel = &panel,
        .theme = &theme,
        .metrics = &metrics,
        .sceneContext = &sceneContext,
        .sceneViewport = &sceneViewport,
    };
    GdiBackBufferRenderer::Paint(window, &PaintBackBuffer, &context);
    if (panel.kind == DockPanelKind::Scene && sceneContext.ViewportPreview(panel.id).ToolbarDropdown() != EditorViewportToolbarDropdown::None) {
        RECT content{};
        GetClientRect(window, &content);
        content.top += metrics.floatingChromeHeight;
        FloatingSceneToolbarDropdownOverlay().Show(window, content, panel.id, theme, sceneContext);
    } else {
        FloatingSceneToolbarDropdownOverlay().Hide();
    }
    if (panel.kind != DockPanelKind::Console) {
        ConsoleDetailTextOverlay::Hide(window);
    }
}

} // namespace kb::editor

#endif
