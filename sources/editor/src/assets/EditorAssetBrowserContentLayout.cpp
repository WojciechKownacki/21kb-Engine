#include "assets/EditorAssetBrowserContentLayout.hpp"

#if defined(_WIN32)
#include <algorithm>

namespace kb::editor {

RECT EditorAssetBrowserContentLayout::FolderRowRect(const EditorAssetBrowserLayoutRects& layout, int row) noexcept {
    return RECT{ layout.tree.left + 8, layout.tree.top + 10 + row * EditorAssetBrowserLayout::RowHeight, layout.tree.right - 8, layout.tree.top + 10 + (row + 1) * EditorAssetBrowserLayout::RowHeight };
}

RECT EditorAssetBrowserContentLayout::AssetListRowRect(const EditorAssetBrowserLayoutRects& layout, int row) noexcept {
    const int top = layout.assetView.top + EditorAssetBrowserLayout::AssetHeaderHeight + row * EditorAssetBrowserLayout::RowHeight;
    return RECT{ layout.assetView.left + 8, top, layout.assetView.right - 8, top + EditorAssetBrowserLayout::RowHeight };
}

RECT EditorAssetBrowserContentLayout::AssetTileRect(const EditorAssetBrowserLayoutRects& layout, int index, float scale) noexcept {
    const int tileWidth = TileWidth(scale);
    const int tileHeight = TileHeight(scale);
    const int columns = AssetTileColumnCount(layout, scale);
    const int column = index % columns;
    const int row = index / columns;
    const int left = layout.assetView.left + 8 + column * (tileWidth + 5);
    const int top = layout.assetView.top + 8 + row * (tileHeight + 5);
    return RECT{ left, top, left + tileWidth, top + tileHeight };
}

int EditorAssetBrowserContentLayout::AssetTileColumnCount(const EditorAssetBrowserLayoutRects& layout, float scale) noexcept {
    const int tileWidth = TileWidth(scale);
    const int width = std::max(1, static_cast<int>(layout.assetView.right - layout.assetView.left - 16));
    return std::max(1, width / (tileWidth + 5));
}

int EditorAssetBrowserContentLayout::TileWidth(float scale) noexcept {
    return std::clamp(static_cast<int>(static_cast<float>(EditorAssetBrowserLayout::BaseTileWidth) * scale), 70, 168);
}

int EditorAssetBrowserContentLayout::TileHeight(float scale) noexcept {
    return std::clamp(static_cast<int>(static_cast<float>(EditorAssetBrowserLayout::BaseTileHeight) * scale), 90, 214);
}

} // namespace kb::editor

#endif
