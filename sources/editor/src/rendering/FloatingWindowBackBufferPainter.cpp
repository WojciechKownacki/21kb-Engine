#include "rendering/FloatingWindowBackBufferPainter.hpp"

#if defined(_WIN32)
#include "rendering/FloatingEditorWindowRenderer.hpp"
#include "rendering/GdiBackBufferRenderer.hpp"
#include "rendering/GdiDrawing.hpp"

namespace kb::editor {
namespace {

struct FloatingWindowPaintContext {
    const DockPanel* panel = nullptr;
    const EditorTheme* theme = nullptr;
    const EditorMetrics* metrics = nullptr;
    const EditorSceneContext* sceneContext = nullptr;
};

void PaintBackBuffer(const GdiBackBufferPaintContext& paint, void* context) {
    auto* paintContext = static_cast<FloatingWindowPaintContext*>(context);
    GdiDrawing::FillRectColor(paint.dc, paint.client, GdiDrawing::ToColorRef(paintContext->theme->background));
    SetBkMode(paint.dc, TRANSPARENT);
    FloatingEditorWindowRenderer{}.Paint(paint.dc, paint.client, *paintContext->panel, *paintContext->theme, *paintContext->metrics, *paintContext->sceneContext);
}

} // namespace

void FloatingWindowBackBufferPainter::Paint(HWND window, const DockPanel& panel, const EditorTheme& theme, const EditorMetrics& metrics, const EditorSceneContext& sceneContext) {
    FloatingWindowPaintContext context{
        .panel = &panel,
        .theme = &theme,
        .metrics = &metrics,
        .sceneContext = &sceneContext,
    };
    GdiBackBufferRenderer::Paint(window, &PaintBackBuffer, &context);
}

} // namespace kb::editor

#endif
