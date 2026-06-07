#include "app/project_files/EditorProjectFilesMouseWheelController.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserLayout.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "rendering/ProjectFilesOverlayRenderer.hpp"
#include "scene/EditorSceneContext.hpp"

#include <algorithm>

namespace kb::editor {
namespace {

[[nodiscard]] bool PointInRect(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] int ProjectFilesAssetContentHeight(const EditorAssetBrowserLayoutRects& layout, const EditorAssetBrowserState& state, const kb::assets::AssetManager& manager) {
    const int itemCount = static_cast<int>(state.ChildFolderRows(manager).size() + state.AssetRows(manager).size())
        + (state.TextEditMode() == EditorAssetTextEditMode::NewFolder ? 1 : 0);
    if (state.ViewMode() == EditorAssetViewMode::Tiles) {
        constexpr int tileGap = 5;
        const int columns = EditorAssetBrowserLayout::AssetTileColumnCount(layout, state.ThumbnailScale());
        const int rows = (itemCount + columns - 1) / std::max(1, columns);
        return rows * (EditorAssetBrowserLayout::TileHeight(state.ThumbnailScale()) + tileGap);
    }
    return itemCount * EditorAssetBrowserLayout::RowHeight;
}

[[nodiscard]] int ProjectFilesAssetViewportHeight(const EditorAssetBrowserLayoutRects& layout, const EditorAssetBrowserState& state) noexcept {
    const RECT viewport = EditorAssetBrowserLayout::AssetViewportRect(layout);
    int height = static_cast<int>(viewport.bottom - viewport.top);
    if (state.ViewMode() == EditorAssetViewMode::List) {
        height -= EditorAssetBrowserLayout::AssetHeaderHeight;
    }
    return std::max(1, height);
}

} // namespace

EditorProjectFilesMouseWheelController::EditorProjectFilesMouseWheelController(EditorSceneContext& sceneContext) noexcept
    : sceneContext_(sceneContext) {}

bool EditorProjectFilesMouseWheelController::HandleMouseWheel(const RECT& content, int x, int y, int wheelDelta) {
    if (!PointInRect(content, x, y) || wheelDelta == 0) {
        return false;
    }
    EditorAssetBrowserState& state = sceneContext_.AssetBrowser();
    kb::assets::AssetManager& manager = sceneContext_.Scene().Assets().Manager();
    const EditorAssetBrowserLayoutRects layout = EditorAssetBrowserLayout::Build(content, state.TreeWidth());
    const int notches = wheelDelta / WHEEL_DELTA;
    const int direction = notches != 0 ? notches : (wheelDelta > 0 ? 1 : -1);

    const RECT deleteList = ProjectFilesOverlayRenderer::DeleteConfirmListRect(content, state);
    if (state.IsDeleteConfirmOpen() && PointInRect(deleteList, x, y)) {
        const int maxOffset = ProjectFilesOverlayRenderer::DeleteConfirmMaxScroll(content, state, manager);
        static_cast<void>(state.SetDeleteConfirmListScrollOffset(
            state.DeleteConfirmListScrollOffset() - direction * ProjectFilesOverlayRenderer::DeleteConfirmListRowHeight() * 3,
            maxOffset));
        return true;
    }

    if (PointInRect(layout.tree, x, y)) {
        const RECT viewport = EditorAssetBrowserLayout::TreeViewportRect(layout);
        const int viewportHeight = static_cast<int>(viewport.bottom - viewport.top);
        const int contentHeight = static_cast<int>(state.FolderRows(manager).size()) * EditorAssetBrowserLayout::RowHeight;
        const int maxOffset = std::max(0, contentHeight - viewportHeight);
        state.SetTreeScrollOffset(state.TreeScrollOffset() - direction * EditorAssetBrowserLayout::RowHeight * 3, maxOffset);
        return true;
    }

    if (PointInRect(layout.assetView, x, y)) {
        const int contentHeight = ProjectFilesAssetContentHeight(layout, state, manager);
        const int viewportHeight = ProjectFilesAssetViewportHeight(layout, state);
        const int maxOffset = std::max(0, contentHeight - viewportHeight);
        const int step = state.ViewMode() == EditorAssetViewMode::Tiles
            ? std::max(1, EditorAssetBrowserLayout::TileHeight(state.ThumbnailScale()) / 2)
            : EditorAssetBrowserLayout::RowHeight * 3;
        state.SetContentScrollOffset(state.ContentScrollOffset() - direction * step, maxOffset);
        return true;
    }
    return false;
}

} // namespace kb::editor

#endif
