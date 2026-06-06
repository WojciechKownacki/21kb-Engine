#include "assets/EditorAssetBrowserContentHitTester.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserGeometry.hpp"
#include "assets/EditorAssetBrowserState.hpp"

#include <algorithm>
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
        constexpr int tileGap = 5;
        const int columns = EditorAssetBrowserLayout::AssetTileColumnCount(layout, state.ThumbnailScale());
        const int stepY = EditorAssetBrowserLayout::TileHeight(state.ThumbnailScale()) + tileGap;
        const int scrolledY = y + state.ContentScrollOffset();
        const RECT viewport = EditorAssetBrowserLayout::AssetViewportRect(layout);
        if (EditorAssetBrowserGeometry::Contains(viewport, x, y)) {
            const int column = std::clamp((x - static_cast<int>(viewport.left)) / std::max(1, EditorAssetBrowserLayout::TileWidth(state.ThumbnailScale()) + tileGap), 0, columns - 1);
            const int row = std::max(0, (scrolledY - static_cast<int>(viewport.top)) / std::max(1, stepY));
            const int globalIndex = row * columns + column;
            const RECT tile = EditorAssetBrowserLayout::AssetTileRect(layout, globalIndex, state.ThumbnailScale());
            RECT visibleTile = tile;
            OffsetRect(&visibleTile, 0, -state.ContentScrollOffset());
            if (EditorAssetBrowserGeometry::Contains(visibleTile, x, y)) {
                if (globalIndex < static_cast<int>(childFolders.size())) {
                    return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::ContentFolder, .index = static_cast<std::size_t>(globalIndex) };
                }
            }
        }
    } else {
        const RECT viewport = EditorAssetBrowserLayout::AssetViewportRect(layout);
        const int bodyTop = viewport.top + EditorAssetBrowserLayout::AssetHeaderHeight;
        if (x >= viewport.left && x < viewport.right && y >= bodyTop && y < viewport.bottom) {
            const int globalRow = std::max(0, (y - bodyTop + state.ContentScrollOffset()) / EditorAssetBrowserLayout::RowHeight);
            if (globalRow < static_cast<int>(childFolders.size())) {
                return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::ContentFolder, .index = static_cast<std::size_t>(globalRow) };
            }
        }
    }

    const std::size_t editRowCount = state.TextEditMode() == EditorAssetTextEditMode::NewFolder ? 1U : 0U;
    const std::size_t assetOffset = childFolders.size() + editRowCount;
    const std::vector<EditorAssetItemRow> assets = state.AssetRows(manager);
    if (state.ViewMode() == EditorAssetViewMode::Tiles) {
        constexpr int tileGap = 5;
        const int columns = EditorAssetBrowserLayout::AssetTileColumnCount(layout, state.ThumbnailScale());
        const int stepY = EditorAssetBrowserLayout::TileHeight(state.ThumbnailScale()) + tileGap;
        const RECT viewport = EditorAssetBrowserLayout::AssetViewportRect(layout);
        if (EditorAssetBrowserGeometry::Contains(viewport, x, y)) {
            const int column = std::clamp((x - static_cast<int>(viewport.left)) / std::max(1, EditorAssetBrowserLayout::TileWidth(state.ThumbnailScale()) + tileGap), 0, columns - 1);
            const int row = std::max(0, (y + state.ContentScrollOffset() - static_cast<int>(viewport.top)) / std::max(1, stepY));
            const int globalIndex = row * columns + column;
            const RECT tile = EditorAssetBrowserLayout::AssetTileRect(layout, globalIndex, state.ThumbnailScale());
            RECT visibleTile = tile;
            OffsetRect(&visibleTile, 0, -state.ContentScrollOffset());
            if (EditorAssetBrowserGeometry::Contains(visibleTile, x, y) && globalIndex >= static_cast<int>(assetOffset)) {
                const int assetIndex = globalIndex - static_cast<int>(assetOffset);
                if (assetIndex < static_cast<int>(assets.size())) {
                    return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::Asset, .index = static_cast<std::size_t>(assetIndex) };
                }
            }
        }
    } else {
        const RECT viewport = EditorAssetBrowserLayout::AssetViewportRect(layout);
        const int bodyTop = viewport.top + EditorAssetBrowserLayout::AssetHeaderHeight;
        if (x >= viewport.left && x < viewport.right && y >= bodyTop && y < viewport.bottom) {
            const int globalRow = std::max(0, (y - bodyTop + state.ContentScrollOffset()) / EditorAssetBrowserLayout::RowHeight);
            if (globalRow >= static_cast<int>(assetOffset)) {
                const int assetIndex = globalRow - static_cast<int>(assetOffset);
                if (assetIndex < static_cast<int>(assets.size())) {
                    return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::Asset, .index = static_cast<std::size_t>(assetIndex) };
                }
            }
        }
    }
    return std::nullopt;
}

} // namespace kb::editor

#endif
