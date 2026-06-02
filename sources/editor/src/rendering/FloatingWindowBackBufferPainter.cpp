#include "rendering/FloatingWindowBackBufferPainter.hpp"

#if defined(_WIN32)
#include "rendering/EditorSurfacePainter.hpp"
#include "rendering/FloatingEditorWindowRenderer.hpp"
#include "rendering/GdiBackBufferRenderer.hpp"
#include "rendering/GdiDrawing.hpp"

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
    EditorSurfacePainter::Fill(paint.dc, paint.client, *paintContext->theme, EditorSurfaceKind::AppBackground);
    SetBkMode(paint.dc, TRANSPARENT);
    FloatingEditorWindowRenderer{}.Paint(paint.dc, paintContext->window, paint.client, *paintContext->panel, *paintContext->theme, *paintContext->metrics, *paintContext->sceneContext, *paintContext->sceneViewport);
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
    sceneViewport.BeginPaintLayout(window);
    GdiBackBufferRenderer::Paint(window, &PaintBackBuffer, &context);
    sceneViewport.EndPaintLayout();
}

} // namespace kb::editor

#endif
