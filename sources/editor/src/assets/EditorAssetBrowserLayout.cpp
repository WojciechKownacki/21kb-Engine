#include "assets/EditorAssetBrowserLayout.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserContentLayout.hpp"
#include "assets/EditorAssetBrowserContextMenuLayout.hpp"
#include "assets/EditorAssetBrowserPanelLayoutBuilder.hpp"

namespace kb::editor {

EditorAssetBrowserLayoutRects EditorAssetBrowserLayout::Build(const RECT& content) noexcept {
    return EditorAssetBrowserPanelLayoutBuilder::Build(content);
}

EditorAssetBrowserLayoutRects EditorAssetBrowserLayout::Build(const RECT& content, int treeWidth) noexcept {
    return EditorAssetBrowserPanelLayoutBuilder::Build(content, treeWidth);
}

RECT EditorAssetBrowserLayout::FolderRowRect(const EditorAssetBrowserLayoutRects& layout, int row) noexcept {
    return EditorAssetBrowserContentLayout::FolderRowRect(layout, row);
}

RECT EditorAssetBrowserLayout::AssetListRowRect(const EditorAssetBrowserLayoutRects& layout, int row) noexcept {
    return EditorAssetBrowserContentLayout::AssetListRowRect(layout, row);
}

RECT EditorAssetBrowserLayout::AssetTileRect(const EditorAssetBrowserLayoutRects& layout, int index, float scale) noexcept {
    return EditorAssetBrowserContentLayout::AssetTileRect(layout, index, scale);
}

int EditorAssetBrowserLayout::AssetTileColumnCount(const EditorAssetBrowserLayoutRects& layout, float scale) noexcept {
    return EditorAssetBrowserContentLayout::AssetTileColumnCount(layout, scale);
}

RECT EditorAssetBrowserLayout::TreeViewportRect(const EditorAssetBrowserLayoutRects& layout) noexcept {
    return EditorAssetBrowserContentLayout::TreeViewportRect(layout);
}

RECT EditorAssetBrowserLayout::AssetViewportRect(const EditorAssetBrowserLayoutRects& layout) noexcept {
    return EditorAssetBrowserContentLayout::AssetViewportRect(layout);
}

RECT EditorAssetBrowserLayout::TreeScrollbarTrackRect(const EditorAssetBrowserLayoutRects& layout) noexcept {
    return EditorAssetBrowserContentLayout::TreeScrollbarTrackRect(layout);
}

RECT EditorAssetBrowserLayout::AssetScrollbarTrackRect(const EditorAssetBrowserLayoutRects& layout) noexcept {
    return EditorAssetBrowserContentLayout::AssetScrollbarTrackRect(layout);
}

RECT EditorAssetBrowserLayout::ScrollbarThumbRect(const RECT& track, int viewportHeight, int contentHeight, int offset) noexcept {
    return EditorAssetBrowserContentLayout::ScrollbarThumbRect(track, viewportHeight, contentHeight, offset);
}

RECT EditorAssetBrowserLayout::ContextMenuRect(const RECT& content, int x, int y, int itemCount) noexcept {
    return EditorAssetBrowserContextMenuLayout::ContextMenuRect(content, x, y, itemCount);
}

RECT EditorAssetBrowserLayout::ContextMenuItemRect(const RECT& menu, int index) noexcept {
    return EditorAssetBrowserContextMenuLayout::ContextMenuItemRect(menu, index);
}

RECT EditorAssetBrowserLayout::FilterMenuRect(const EditorAssetBrowserLayoutRects& layout) noexcept {
    constexpr int width = 168;
    constexpr int itemCount = 2;
    constexpr int height = ContextMenuPadding * 2
        + itemCount * ContextMenuRowHeight
        + (itemCount - 1) * ContextMenuSeparatorHeight;
    return RECT{
        layout.filtersButton.left,
        layout.filtersButton.top - 6 - height,
        layout.filtersButton.left + width,
        layout.filtersButton.top - 6,
    };
}

RECT EditorAssetBrowserLayout::FilterMenuItemRect(const RECT& menu, int index) noexcept {
    return ContextMenuItemRect(menu, index);
}

int EditorAssetBrowserLayout::TileWidth(float scale) noexcept {
    return EditorAssetBrowserContentLayout::TileWidth(scale);
}

int EditorAssetBrowserLayout::TileHeight(float scale) noexcept {
    return EditorAssetBrowserContentLayout::TileHeight(scale);
}

} // namespace kb::editor

#endif
