#include "app/scene_viewport/EditorSceneViewportToolbarPointerController.hpp"

#include "rendering/EditorPanelContentResolver.hpp"
#include "rendering/EditorSceneBgfxViewport.hpp"
#include "rendering/SceneViewportToolbarRenderer.hpp"
#include "scene/EditorSceneContext.hpp"

namespace kb::editor {
namespace {

[[nodiscard]] bool PointInRect(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

} // namespace

EditorSceneViewportToolbarPointerController::EditorSceneViewportToolbarPointerController(EditorSceneContext& sceneContext, EditorSceneBgfxViewport& sceneViewport) noexcept
    : sceneContext_(sceneContext)
    , sceneViewport_(sceneViewport) {}

bool EditorSceneViewportToolbarPointerController::HandlePointerDown(const EditorResolvedPanelContent& panelContent, int x, int y) {
    const SceneViewportToolbarRects toolbar = SceneViewportToolbarRenderer::Resolve(panelContent.content);
    EditorViewportPreviewState& preview = sceneContext_.ViewportPreview(panelContent.panelId);
    if (PointInRect(toolbar.profileButton, x, y)) {
        preview.CycleProfile();
        sceneViewport_.RequestPresent();
        return true;
    }
    if (PointInRect(toolbar.gridToggleButton, x, y)) {
        preview.ToggleGridVisible();
        sceneViewport_.RequestPresent();
        return true;
    }
    if (PointInRect(toolbar.gridStepButton, x, y)) {
        preview.CycleGridSpacing();
        sceneViewport_.RequestPresent();
        return true;
    }
    if (PointInRect(toolbar.snapToggleButton, x, y)) {
        preview.ToggleSnapEnabled();
        sceneViewport_.RequestPresent();
        return true;
    }
    if (PointInRect(toolbar.snapStepButton, x, y)) {
        preview.CycleSnapStep();
        sceneViewport_.RequestPresent();
        return true;
    }
    return false;
}

} // namespace kb::editor
