#include "assets/EditorAssetBrowserContentLayout.hpp"

#if defined(_WIN32)
#include <algorithm>

namespace kb::editor {

RECT EditorAssetBrowserContentLayout::FolderRowRect(const EditorAssetBrowserLayoutRects& layout, int row) noexcept {
    const RECT viewport = TreeViewportRect(layout);
    return RECT{ viewport.left, viewport.top + row * EditorAssetBrowserLayout::RowHeight, viewport.right, viewport.top + (row + 1) * EditorAssetBrowserLayout::RowHeight };
}

RECT EditorAssetBrowserContentLayout::AssetListRowRect(const EditorAssetBrowserLayoutRects& layout, int row) noexcept {
    const RECT viewport = AssetViewportRect(layout);
    const int top = viewport.top + EditorAssetBrowserLayout::AssetHeaderHeight + row * EditorAssetBrowserLayout::RowHeight;
    return RECT{ viewport.left, top, viewport.right, top + EditorAssetBrowserLayout::RowHeight };
}

RECT EditorAssetBrowserContentLayout::AssetTileRect(const EditorAssetBrowserLayoutRects& layout, int index, float scale) noexcept {
    const int tileWidth = TileWidth(scale);
    const int tileHeight = TileHeight(scale);
    const int columns = AssetTileColumnCount(layout, scale);
    const int column = index % columns;
    const int row = index / columns;
    constexpr int tileGap = 5;
    const RECT viewport = AssetViewportRect(layout);
    const int left = viewport.left + column * (tileWidth + tileGap);
    const int top = viewport.top + row * (tileHeight + tileGap);
    return RECT{ left, top, left + tileWidth, top + tileHeight };
}

int EditorAssetBrowserContentLayout::AssetTileColumnCount(const EditorAssetBrowserLayoutRects& layout, float scale) noexcept {
    const int tileWidth = TileWidth(scale);
    constexpr int tileGap = 5;
    const RECT viewport = AssetViewportRect(layout);
    const int width = std::max(1, static_cast<int>(viewport.right - viewport.left));
    return std::max(1, (width + tileGap) / (tileWidth + tileGap));
}

int EditorAssetBrowserContentLayout::TileWidth(float scale) noexcept {
    return std::clamp(static_cast<int>(static_cast<float>(EditorAssetBrowserLayout::BaseTileWidth) * scale), 70, 168);
}

int EditorAssetBrowserContentLayout::TileHeight(float scale) noexcept {
    return std::clamp(static_cast<int>(static_cast<float>(EditorAssetBrowserLayout::BaseTileHeight) * scale), 90, 214);
}

RECT EditorAssetBrowserContentLayout::TreeViewportRect(const EditorAssetBrowserLayoutRects& layout) noexcept {
    return RECT{
        layout.tree.left + 1,
        layout.toolbar.bottom,
        std::max(layout.tree.left + 1, layout.tree.right - EditorAssetBrowserLayout::ScrollbarWidth),
        layout.tree.bottom - 1,
    };
}

RECT EditorAssetBrowserContentLayout::AssetViewportRect(const EditorAssetBrowserLayoutRects& layout) noexcept {
    return RECT{
        layout.assetView.left + EditorAssetBrowserLayout::ContentInset,
        layout.assetView.top + EditorAssetBrowserLayout::ContentInset,
        std::max(layout.assetView.left + EditorAssetBrowserLayout::ContentInset, layout.assetView.right - EditorAssetBrowserLayout::ScrollbarWidth - EditorAssetBrowserLayout::ContentInset),
        layout.assetView.bottom - EditorAssetBrowserLayout::ContentInset,
    };
}

RECT EditorAssetBrowserContentLayout::TreeScrollbarTrackRect(const EditorAssetBrowserLayoutRects& layout) noexcept {
    return RECT{ layout.tree.right - EditorAssetBrowserLayout::ScrollbarWidth, layout.toolbar.bottom, layout.tree.right - 1, layout.tree.bottom - 1 };
}

RECT EditorAssetBrowserContentLayout::AssetScrollbarTrackRect(const EditorAssetBrowserLayoutRects& layout) noexcept {
    return RECT{ layout.assetView.right - EditorAssetBrowserLayout::ScrollbarWidth - 1, layout.assetView.top + EditorAssetBrowserLayout::ContentInset, layout.assetView.right - 1, layout.assetView.bottom - EditorAssetBrowserLayout::ContentInset };
}

RECT EditorAssetBrowserContentLayout::ScrollbarThumbRect(const RECT& track, int viewportHeight, int contentHeight, int offset) noexcept {
    const int trackHeight = std::max(1, static_cast<int>(track.bottom - track.top));
    if (contentHeight <= viewportHeight || viewportHeight <= 0) {
        return RECT{ track.left, track.top, track.right, track.bottom };
    }
    const int minThumbHeight = std::min(24, trackHeight);
    const int proportionalThumb = (viewportHeight * trackHeight) / std::max(1, contentHeight);
    const int thumbHeight = std::clamp(proportionalThumb, minThumbHeight, trackHeight);
    const int maxOffset = std::max(1, contentHeight - viewportHeight);
    const int travel = std::max(1, trackHeight - thumbHeight);
    const int top = track.top + (std::clamp(offset, 0, maxOffset) * travel) / maxOffset;
    return RECT{ track.left, top, track.right, top + thumbHeight };
}

} // namespace kb::editor

#endif
