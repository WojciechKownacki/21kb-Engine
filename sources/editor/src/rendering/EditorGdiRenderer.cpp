#include "rendering/EditorGdiRenderer.hpp"

#if defined(_WIN32)
#include "rendering/FloatingWindowBackBufferPainter.hpp"
#include "rendering/MainWindowBackBufferPainter.hpp"

namespace kb::editor {

void EditorGdiRenderer::Paint(HWND window, const EditorDockModel& dockModel, const EditorTheme& theme, const EditorMetrics& metrics, const EditorSceneContext& sceneContext, const DockDropPreview* preview, const EditorPointerDragState& drag) const {
    MainWindowBackBufferPainter::Paint(window, dockModel, theme, metrics, sceneContext, preview, drag);
}

void EditorGdiRenderer::PaintFloating(HWND window, const DockPanel& panel, const EditorTheme& theme, const EditorMetrics& metrics, const EditorSceneContext& sceneContext) const {
    FloatingWindowBackBufferPainter::Paint(window, panel, theme, metrics, sceneContext);
}

} // namespace kb::editor

#endif
