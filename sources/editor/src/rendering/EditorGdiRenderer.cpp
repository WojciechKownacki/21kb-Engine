#include "rendering/EditorGdiRenderer.hpp"

#if defined(_WIN32)
#include "rendering/FloatingWindowBackBufferPainter.hpp"
#include "rendering/MainWindowBackBufferPainter.hpp"

namespace kb::editor {

void EditorGdiRenderer::Paint(HWND window, const EditorDockModel& dockModel, const EditorTheme& theme, const EditorMetrics& metrics, const EditorSceneContext& sceneContext, const DockDropPreview* preview, const EditorPointerDragState& drag, const EditorRenderBackendSettings& renderBackendSettings, const EditorPlayModeState& playMode, const EditorShellInteractionState& shellInteraction, EditorSceneBgfxViewport& sceneViewport) const {
    MainWindowBackBufferPainter::Paint(window, dockModel, theme, metrics, sceneContext, preview, drag, renderBackendSettings, playMode, shellInteraction, sceneViewport);
}

void EditorGdiRenderer::PaintFloating(HWND window, const DockPanel& panel, const EditorTheme& theme, const EditorMetrics& metrics, const EditorSceneContext& sceneContext, EditorSceneBgfxViewport& sceneViewport) const {
    FloatingWindowBackBufferPainter::Paint(window, panel, theme, metrics, sceneContext, sceneViewport);
}

} // namespace kb::editor

#endif
