#include "app/game_view/EditorGameViewPointerController.hpp"

#if defined(_WIN32)
#include "rendering/EditorSceneBgfxViewport.hpp"
#include "rendering/GamePanelContentRenderer.hpp"
#include "scene/EditorSceneContext.hpp"

namespace kb::editor {
namespace {

[[nodiscard]] bool PointInRect(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

} // namespace

EditorGameViewPointerController::EditorGameViewPointerController(EditorSceneContext& sceneContext, EditorSceneBgfxViewport& sceneViewport) noexcept
    : sceneContext_(sceneContext), sceneViewport_(sceneViewport) {}

bool EditorGameViewPointerController::HandlePointerDown(const EditorResolvedPanelContent& panel, int x, int y) {
    const GameViewRects rects = GamePanelContentRenderer::Resolve(panel.content);
    if (!PointInRect(rects.toolbar, x, y)) {
        return false;
    }
    EditorConfiguration configuration = sceneContext_.EditorConfig();
    EditorPlayPreferences& play = configuration.play;
    if (const std::optional<EditorGameViewDevice> device = GamePanelContentRenderer::DeviceAt(rects, x, y)) {
        play.gameViewDevice = *device;
    } else if (PointInRect(rects.rotateButton, x, y)) {
        // A computer cannot be turned; its button stays on the bar without an effect.
        if (EditorGameView::CanRotate(play.gameViewDevice)) {
            play.gameViewPortrait = !play.gameViewPortrait;
        }
    } else if (PointInRect(rects.safeAreaButton, x, y)) {
        play.gameViewSafeArea = !play.gameViewSafeArea;
    } else {
        return true;
    }
    static_cast<void>(sceneContext_.SaveEditorConfig(std::move(configuration)));
    sceneViewport_.RequestPresent();
    return true;
}

} // namespace kb::editor

#endif
