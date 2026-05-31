#include "assets/EditorAssetBrowserContentHitTester.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserGeometry.hpp"
#include "assets/EditorAssetBrowserState.hpp"

#include <vector>

namespace kb::editor {

std::optional<EditorAssetBrowserHit> EditorAssetBrowserContentHitTester::HitTest(
    const EditorAssetBrowserLayoutRects& layout,
    int x,
    int y,
    const EditorAssetBrowserState& state,
    const kb::assets::AssetManager& manager) {
    const std::vector<EditorAssetFolderRow> childFolders = state.ChildFolderRows(manager);
    if (state.ViewMode() == EditorAssetViewMode::Tiles) {
        for (std::size_t index = 0; index < childFolders.size(); ++index) {
            const RECT tile = EditorAssetBrowserLayout::AssetTileRect(layout, static_cast<int>(index), state.ThumbnailScale());
            if (EditorAssetBrowserGeometry::Contains(tile, x, y)) {
                return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::ContentFolder, .index = index };
            }
        }
    } else {
        for (std::size_t index = 0; index < childFolders.size(); ++index) {
            const RECT row = EditorAssetBrowserLayout::AssetListRowRect(layout, static_cast<int>(index));
            if (EditorAssetBrowserGeometry::Contains(row, x, y)) {
                return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::ContentFolder, .index = index };
            }
        }
    }

    const std::size_t editRowCount = state.TextEditMode() == EditorAssetTextEditMode::NewFolder ? 1U : 0U;
    const std::size_t assetOffset = childFolders.size() + editRowCount;
    const std::vector<EditorAssetItemRow> assets = state.AssetRows(manager);
    if (state.ViewMode() == EditorAssetViewMode::Tiles) {
        for (std::size_t index = 0; index < assets.size(); ++index) {
            const auto tileIndex = static_cast<int>(index + assetOffset);
            const RECT tile = EditorAssetBrowserLayout::AssetTileRect(layout, tileIndex, state.ThumbnailScale());
            if (EditorAssetBrowserGeometry::Contains(tile, x, y)) {
                return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::Asset, .index = index };
            }
        }
    } else {
        for (std::size_t index = 0; index < assets.size(); ++index) {
            const auto rowIndex = static_cast<int>(index + assetOffset);
            const RECT row = EditorAssetBrowserLayout::AssetListRowRect(layout, rowIndex);
            if (EditorAssetBrowserGeometry::Contains(row, x, y)) {
                return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::Asset, .index = index };
            }
        }
    }
    return std::nullopt;
}

} // namespace kb::editor

#endif
