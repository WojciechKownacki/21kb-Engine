#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

struct EditorAssetBrowserLayoutRects {
    RECT frame{};
    RECT toolbar{};
    RECT path{};
    RECT search{};
    RECT refreshButton{};
    RECT newFolderButton{};
    RECT filtersButton{};
    RECT renameButton{};
    RECT deleteButton{};
    RECT tree{};
    RECT treeSplitter{};
    RECT assetView{};
    RECT bottomBar{};
    RECT sortButton{};
    RECT sortMenu{};
    RECT sortNameItem{};
    RECT sortTypeItem{};
    RECT sortPathItem{};
    RECT listButton{};
    RECT tileButton{};
    RECT recursiveButton{};
    RECT sliderTrack{};
    RECT sliderThumb{};
};

class EditorAssetBrowserLayout {
public:
    EditorAssetBrowserLayout() = delete;

    [[nodiscard]] static EditorAssetBrowserLayoutRects Build(const RECT& content) noexcept;
    [[nodiscard]] static EditorAssetBrowserLayoutRects Build(const RECT& content, int treeWidth) noexcept;
    [[nodiscard]] static RECT FolderRowRect(const EditorAssetBrowserLayoutRects& layout, int row) noexcept;
    [[nodiscard]] static RECT AssetListRowRect(const EditorAssetBrowserLayoutRects& layout, int row) noexcept;
    [[nodiscard]] static RECT AssetTileRect(const EditorAssetBrowserLayoutRects& layout, int index, float scale) noexcept;
    [[nodiscard]] static int AssetTileColumnCount(const EditorAssetBrowserLayoutRects& layout, float scale) noexcept;
    [[nodiscard]] static RECT TreeViewportRect(const EditorAssetBrowserLayoutRects& layout) noexcept;
    [[nodiscard]] static RECT AssetViewportRect(const EditorAssetBrowserLayoutRects& layout) noexcept;
    [[nodiscard]] static RECT TreeScrollbarTrackRect(const EditorAssetBrowserLayoutRects& layout) noexcept;
    [[nodiscard]] static RECT AssetScrollbarTrackRect(const EditorAssetBrowserLayoutRects& layout) noexcept;
    [[nodiscard]] static RECT ScrollbarThumbRect(const RECT& track, int viewportHeight, int contentHeight, int offset) noexcept;
    [[nodiscard]] static RECT ContextMenuRect(const RECT& content, int x, int y, int itemCount) noexcept;
    [[nodiscard]] static RECT ContextMenuItemRect(const RECT& menu, int index) noexcept;
    [[nodiscard]] static RECT FilterMenuRect(const EditorAssetBrowserLayoutRects& layout) noexcept;
    [[nodiscard]] static RECT FilterMenuItemRect(const RECT& menu, int index) noexcept;
    [[nodiscard]] static int TileWidth(float scale) noexcept;
    [[nodiscard]] static int TileHeight(float scale) noexcept;

    static constexpr int RowHeight = 22;
    static constexpr int AssetHeaderHeight = 34;
    static constexpr int BaseTileWidth = 96;
    static constexpr int BaseTileHeight = 110;
    static constexpr int ContextMenuWidth = 168;
    static constexpr int ContextMenuRowHeight = 26;
    static constexpr int ContextMenuPadding = 4;
    static constexpr int ContextMenuSeparatorHeight = 7;
    static constexpr int ContentInset = 5;
    static constexpr int ScrollbarWidth = 12;
};

#endif

} // namespace kb::editor
