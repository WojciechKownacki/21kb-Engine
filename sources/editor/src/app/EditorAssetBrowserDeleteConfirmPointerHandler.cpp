#include "app/EditorAssetBrowserDeleteConfirmPointerHandler.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserState.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "rendering/ProjectFilesOverlayRenderer.hpp"
#include "scene/EditorSceneContext.hpp"

#include <algorithm>

namespace kb::editor {

bool EditorAssetBrowserDeleteConfirmPointerHandler::HandlePointerDown(const RECT& bounds, const EditorAssetBrowserHit& hit, int x, int y, EditorSceneContext& sceneContext) {
    EditorAssetBrowserState& state = sceneContext.AssetBrowser();
    kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    switch (hit.kind) {
    case EditorAssetBrowserHitKind::DeleteConfirmAccept: {
        const bool deleted = sceneContext.DeleteSelectedAssetBrowserItem();
        state.CloseDeleteConfirm();
        return deleted;
    }
    case EditorAssetBrowserHitKind::DeleteConfirmCancel:
        state.CloseDeleteConfirm();
        return true;
    case EditorAssetBrowserHitKind::DeleteConfirmScrollbarThumb:
        state.BeginDeleteConfirmListScrollbarDrag(y);
        return true;
    case EditorAssetBrowserHitKind::DeleteConfirmScrollbarTrack: {
        const RECT thumb = ProjectFilesOverlayRenderer::DeleteConfirmListScrollbarThumbRect(bounds, state, manager);
        const int direction = y < thumb.top ? -1 : 1;
        const RECT viewport = ProjectFilesOverlayRenderer::DeleteConfirmListViewportRect(bounds, state);
        const int page = std::max(ProjectFilesOverlayRenderer::DeleteConfirmListRowHeight(), static_cast<int>(viewport.bottom - viewport.top) - ProjectFilesOverlayRenderer::DeleteConfirmListRowHeight());
        static_cast<void>(state.SetDeleteConfirmListScrollOffset(state.DeleteConfirmListScrollOffset() + direction * page, ProjectFilesOverlayRenderer::DeleteConfirmMaxScroll(bounds, state, manager)));
        return true;
    }
    case EditorAssetBrowserHitKind::DeleteConfirmCheckbox: {
        const std::vector<EditorAssetSelectionSummaryRow> rows = state.DeleteTargetRows(manager);
        if (hit.index < rows.size()) {
            static_cast<void>(state.ToggleDeleteTargetChecked(rows[hit.index].key));
        }
        return true;
    }
    case EditorAssetBrowserHitKind::DeleteConfirmListBody:
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

bool EditorAssetBrowserDeleteConfirmPointerHandler::HandlePointerMove(const RECT& bounds, int x, int y, EditorSceneContext& sceneContext) {
    EditorAssetBrowserState& state = sceneContext.AssetBrowser();
    if (state.IsDeleteConfirmListScrollbarDragging()) {
        kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
        const RECT track = ProjectFilesOverlayRenderer::DeleteConfirmListScrollbarTrackRect(bounds, state);
        const RECT thumb = ProjectFilesOverlayRenderer::DeleteConfirmListScrollbarThumbRect(bounds, state, manager);
        const int trackTravel = std::max(1, static_cast<int>((track.bottom - track.top) - (thumb.bottom - thumb.top)));
        state.DragDeleteConfirmListScrollbar(y, trackTravel, ProjectFilesOverlayRenderer::DeleteConfirmMaxScroll(bounds, state, manager));
        return true;
    }
    if (!state.IsDeleteConfirmDragging()) {
        return false;
    }

    state.DragDeleteConfirm(x, y);
    return true;
}

bool EditorAssetBrowserDeleteConfirmPointerHandler::HandlePointerUp(EditorSceneContext& sceneContext) noexcept {
    EditorAssetBrowserState& state = sceneContext.AssetBrowser();
    if (state.IsDeleteConfirmListScrollbarDragging()) {
        state.EndDeleteConfirmListScrollbarDrag();
        return true;
    }
    if (!state.IsDeleteConfirmDragging()) {
        return false;
    }

    state.EndDeleteConfirmDrag();
    return true;
}

} // namespace kb::editor

#endif
