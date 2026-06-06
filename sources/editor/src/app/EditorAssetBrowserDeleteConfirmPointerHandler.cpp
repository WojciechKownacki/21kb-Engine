#include "app/EditorAssetBrowserDeleteConfirmPointerHandler.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserState.hpp"
#include "scene/EditorSceneContext.hpp"

namespace kb::editor {

bool EditorAssetBrowserDeleteConfirmPointerHandler::HandlePointerDown(const EditorAssetBrowserHit& hit, int x, int y, EditorSceneContext& sceneContext) {
    EditorAssetBrowserState& state = sceneContext.AssetBrowser();
    switch (hit.kind) {
    case EditorAssetBrowserHitKind::DeleteConfirmAccept:
        state.CloseDeleteConfirm();
        return sceneContext.DeleteSelectedAssetBrowserItem();
    case EditorAssetBrowserHitKind::DeleteConfirmCancel:
        state.CloseDeleteConfirm();
        return true;
    case EditorAssetBrowserHitKind::DeleteConfirmBody:
        state.BeginDeleteConfirmDrag(x, y);
        return true;
    default:
        state.CloseDeleteConfirm();
        state.FocusSelection(false);
        return true;
    }
}

bool EditorAssetBrowserDeleteConfirmPointerHandler::HandlePointerMove(int x, int y, EditorSceneContext& sceneContext) {
    EditorAssetBrowserState& state = sceneContext.AssetBrowser();
    if (!state.IsDeleteConfirmDragging()) {
        return false;
    }

    state.DragDeleteConfirm(x, y);
    return true;
}

bool EditorAssetBrowserDeleteConfirmPointerHandler::HandlePointerUp(EditorSceneContext& sceneContext) noexcept {
    EditorAssetBrowserState& state = sceneContext.AssetBrowser();
    if (!state.IsDeleteConfirmDragging()) {
        return false;
    }

    state.EndDeleteConfirmDrag();
    return true;
}

} // namespace kb::editor

#endif
