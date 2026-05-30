#include "rendering/MainWindowBackBufferPainter.hpp"

#if defined(_WIN32)
#include "rendering/DockWorkspaceRenderer.hpp"
#include "rendering/GdiBackBufferRenderer.hpp"
#include "rendering/GdiDrawing.hpp"

namespace kb::editor {
namespace {

struct MainWindowPaintContext {
    const EditorDockModel* dockModel = nullptr;
    const EditorTheme* theme = nullptr;
    const EditorMetrics* metrics = nullptr;
    const EditorSceneContext* sceneContext = nullptr;
    const DockDropPreview* preview = nullptr;
};

void PaintBackBuffer(const GdiBackBufferPaintContext& paint, void* context) {
    auto* paintContext = static_cast<MainWindowPaintContext*>(context);
    GdiDrawing::FillRectColor(paint.dc, paint.client, GdiDrawing::ToColorRef(paintContext->theme->background));
    SetBkMode(paint.dc, TRANSPARENT);
    DockWorkspaceRenderer{}.Paint(paint.dc, paint.width, paint.height, *paintContext->dockModel, *paintContext->theme, *paintContext->metrics, *paintContext->sceneContext, paintContext->preview);
}

} // namespace

void MainWindowBackBufferPainter::Paint(HWND window, const EditorDockModel& dockModel, const EditorTheme& theme, const EditorMetrics& metrics, const EditorSceneContext& sceneContext, const DockDropPreview* preview) {
    MainWindowPaintContext context{
        .dockModel = &dockModel,
        .theme = &theme,
        .metrics = &metrics,
        .sceneContext = &sceneContext,
        .preview = preview,
    };
    GdiBackBufferRenderer::Paint(window, &PaintBackBuffer, &context);
}

} // namespace kb::editor

#endif
