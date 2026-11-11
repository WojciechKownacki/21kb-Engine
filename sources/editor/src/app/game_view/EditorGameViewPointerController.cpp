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
    EditorViewportPreviewState& preview = sceneContext_.ViewportPreview(panel.panelId);
    const bool listOpen = preview.ToolbarDropdown() == EditorViewportToolbarDropdown::GameResolution;
    const GameViewRects rects = GamePanelContentRenderer::Resolve(panel.content, listOpen);

    if (listOpen) {
        if (const std::optional<std::size_t> item = GamePanelContentRenderer::ItemAt(rects, x, y)) {
            EditorConfiguration configuration = sceneContext_.EditorConfig();
            configuration.play.gameViewResolution = EditorGameView::kResolutions[*item];
            preview.CloseToolbarDropdown();
            static_cast<void>(sceneContext_.SaveEditorConfig(std::move(configuration)));
            sceneViewport_.RequestPresent();
            return true;
        }
    }
    if (PointInRect(rects.resolutionButton, x, y)) {
        preview.ToggleToolbarDropdown(EditorViewportToolbarDropdown::GameResolution);
        sceneViewport_.RequestPresent();
        return true;
    }
    const bool onBar = PointInRect(rects.toolbar, x, y) || (listOpen && PointInRect(rects.list, x, y));
    if (listOpen && !onBar) {
        // A press on the picture puts the list away and goes on to the game.
        preview.CloseToolbarDropdown();
        sceneViewport_.RequestPresent();
    }
    return onBar;
}

} // namespace kb::editor

#endif
