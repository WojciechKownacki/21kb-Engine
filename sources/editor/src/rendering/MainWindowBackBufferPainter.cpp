#include "rendering/MainWindowBackBufferPainter.hpp"

#if defined(_WIN32)
#include "rendering/DockWorkspaceRenderer.hpp"
#include "rendering/EditorDragOverlayRenderer.hpp"
#include "rendering/EditorSurfacePainter.hpp"
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
    const EditorPointerDragState* drag = nullptr;
    EditorSceneBgfxViewport* sceneViewport = nullptr;
};

void PaintBackBuffer(const GdiBackBufferPaintContext& paint, void* context) {
    auto* paintContext = static_cast<MainWindowPaintContext*>(context);
    EditorSurfacePainter::Fill(paint.dc, paint.client, *paintContext->theme, EditorSurfaceKind::AppBackground);
    SetBkMode(paint.dc, TRANSPARENT);
    DockWorkspaceRenderer{}.Paint(paint.dc, paint.width, paint.height, *paintContext->dockModel, *paintContext->theme, *paintContext->metrics, *paintContext->sceneContext, paintContext->preview, *paintContext->sceneViewport);
    EditorDragOverlayRenderer{}.Paint(paint.dc, *paintContext->drag, *paintContext->theme, *paintContext->sceneContext);
}

} // namespace

void MainWindowBackBufferPainter::Paint(HWND window, const EditorDockModel& dockModel, const EditorTheme& theme, const EditorMetrics& metrics, const EditorSceneContext& sceneContext, const DockDropPreview* preview, const EditorPointerDragState& drag, EditorSceneBgfxViewport& sceneViewport) {
    MainWindowPaintContext context{
        .dockModel = &dockModel,
        .theme = &theme,
        .metrics = &metrics,
        .sceneContext = &sceneContext,
        .preview = preview,
        .drag = &drag,
        .sceneViewport = &sceneViewport,
    };
    sceneViewport.BeginPaintLayout();
    GdiBackBufferRenderer::Paint(window, &PaintBackBuffer, &context);
    sceneViewport.EndPaintLayout();
}

} // namespace kb::editor

#endif
