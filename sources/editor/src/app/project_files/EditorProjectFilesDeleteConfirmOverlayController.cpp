#include "app/project_files/EditorProjectFilesDeleteConfirmOverlayController.hpp"

#if defined(_WIN32)
#include "app/EditorAssetBrowserDeleteConfirmPointerHandler.hpp"
#include "assets/EditorAssetBrowserOverlayHitTester.hpp"
#include "scene/EditorSceneContext.hpp"

#include <optional>

namespace kb::editor {

EditorProjectFilesDeleteConfirmOverlayController::EditorProjectFilesDeleteConfirmOverlayController(HWND owner, EditorSceneContext& sceneContext) noexcept
    : owner_(owner)
    , sceneContext_(sceneContext) {}

bool EditorProjectFilesDeleteConfirmOverlayController::HandlePointerDown(int x, int y) const {
    if (!sceneContext_.AssetBrowser().IsDeleteConfirmOpen()) {
        return false;
    }

    RECT client{};
    GetClientRect(owner_, &client);
    const std::optional<EditorAssetBrowserHit> hit = EditorAssetBrowserOverlayHitTester::HitTestDeleteConfirm(
        client,
        x,
        y,
        sceneContext_.AssetBrowser(),
        &client);
    return EditorAssetBrowserDeleteConfirmPointerHandler::HandlePointerDown(hit.value_or(EditorAssetBrowserHit{}), x, y, sceneContext_);
}

} // namespace kb::editor

#endif
