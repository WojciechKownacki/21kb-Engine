#include "assets/EditorAssetBrowserLayout.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserContentLayout.hpp"
#include "assets/EditorAssetBrowserContextMenuLayout.hpp"
#include "assets/EditorAssetBrowserPanelLayoutBuilder.hpp"

namespace kb::editor {

EditorAssetBrowserLayoutRects EditorAssetBrowserLayout::Build(const RECT& content) noexcept {
    return EditorAssetBrowserPanelLayoutBuilder::Build(content);
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

RECT EditorAssetBrowserLayout::ContextMenuRect(const RECT& content, int x, int y, int itemCount) noexcept {
    return EditorAssetBrowserContextMenuLayout::ContextMenuRect(content, x, y, itemCount);
}

RECT EditorAssetBrowserLayout::ContextMenuItemRect(const RECT& menu, int index) noexcept {
    return EditorAssetBrowserContextMenuLayout::ContextMenuItemRect(menu, index);
}

int EditorAssetBrowserLayout::TileWidth(float scale) noexcept {
    return EditorAssetBrowserContentLayout::TileWidth(scale);
}

int EditorAssetBrowserLayout::TileHeight(float scale) noexcept {
    return EditorAssetBrowserContentLayout::TileHeight(scale);
}

} // namespace kb::editor

#endif
