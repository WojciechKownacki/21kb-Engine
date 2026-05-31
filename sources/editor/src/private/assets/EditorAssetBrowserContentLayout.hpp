#pragma once

#include "assets/EditorAssetBrowserLayout.hpp"

namespace kb::editor {

#if defined(_WIN32)

class EditorAssetBrowserContentLayout {
public:
    EditorAssetBrowserContentLayout() = delete;

    [[nodiscard]] static RECT FolderRowRect(const EditorAssetBrowserLayoutRects& layout, int row) noexcept;
    [[nodiscard]] static RECT AssetListRowRect(const EditorAssetBrowserLayoutRects& layout, int row) noexcept;
    [[nodiscard]] static RECT AssetTileRect(const EditorAssetBrowserLayoutRects& layout, int index, float scale) noexcept;
    [[nodiscard]] static int AssetTileColumnCount(const EditorAssetBrowserLayoutRects& layout, float scale) noexcept;
    [[nodiscard]] static int TileWidth(float scale) noexcept;
    [[nodiscard]] static int TileHeight(float scale) noexcept;
};

#endif

} // namespace kb::editor
