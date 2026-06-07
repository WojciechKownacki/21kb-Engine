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
    if (!PointInRect(toolbar.profileButton, x, y)) {
        return false;
    }

    sceneContext_.ViewportPreview(panelContent.panelId).CycleProfile();
    sceneViewport_.RequestPresent();
    return true;
}

} // namespace kb::editor
