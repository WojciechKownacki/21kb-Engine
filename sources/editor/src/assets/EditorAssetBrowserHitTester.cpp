#include "assets/EditorAssetBrowserHitTester.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserBreadcrumbHitTester.hpp"
#include "assets/EditorAssetBrowserChromeHitTester.hpp"
#include "assets/EditorAssetBrowserContentHitTester.hpp"
#include "assets/EditorAssetBrowserGeometry.hpp"
#include "assets/EditorAssetBrowserHitPayloadResolver.hpp"
#include "assets/EditorAssetBrowserOverlayHitTester.hpp"
#include "assets/EditorAssetBrowserState.hpp"
#include "assets/EditorAssetBrowserTreeHitTester.hpp"

#include <algorithm>

namespace kb::editor {
namespace {

[[nodiscard]] int AssetContentHeight(const EditorAssetBrowserLayoutRects& layout, const EditorAssetBrowserState& state, const kb::assets::AssetManager& manager) {
    const int folderCount = static_cast<int>(state.ChildFolderRows(manager).size());
    const int assetCount = static_cast<int>(state.AssetRows(manager).size());
    const int editCount = state.TextEditMode() == EditorAssetTextEditMode::NewFolder ? 1 : 0;
    const int itemCount = folderCount + assetCount + editCount;
    if (state.ViewMode() == EditorAssetViewMode::Tiles) {
        constexpr int tileGap = 5;
        const int columns = EditorAssetBrowserLayout::AssetTileColumnCount(layout, state.ThumbnailScale());
        const int rows = (itemCount + columns - 1) / std::max(1, columns);
        return rows * (EditorAssetBrowserLayout::TileHeight(state.ThumbnailScale()) + tileGap);
    }
    return itemCount * EditorAssetBrowserLayout::RowHeight;
}

[[nodiscard]] std::optional<EditorAssetBrowserHit> HitTestScrollbars(
    const EditorAssetBrowserLayoutRects& layout,
    int x,
    int y,
    const EditorAssetBrowserState& state,
    const kb::assets::AssetManager& manager) {
    const int treeContentHeight = static_cast<int>(state.FolderRows(manager).size()) * EditorAssetBrowserLayout::RowHeight;
    const RECT treeViewport = EditorAssetBrowserLayout::TreeViewportRect(layout);
    const int treeViewportHeight = static_cast<int>(treeViewport.bottom - treeViewport.top);
    if (treeContentHeight > treeViewportHeight) {
        const RECT track = EditorAssetBrowserLayout::TreeScrollbarTrackRect(layout);
        const RECT thumb = EditorAssetBrowserLayout::ScrollbarThumbRect(track, treeViewportHeight, treeContentHeight, state.TreeScrollOffset());
        if (EditorAssetBrowserGeometry::Contains(thumb, x, y)) {
            return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::TreeScrollbarThumb };
        }
        if (EditorAssetBrowserGeometry::Contains(track, x, y)) {
            return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::TreeScrollbarTrack };
        }
    }

    const RECT assetViewport = EditorAssetBrowserLayout::AssetViewportRect(layout);
    int assetViewportHeight = static_cast<int>(assetViewport.bottom - assetViewport.top);
    if (state.ViewMode() == EditorAssetViewMode::List) {
        assetViewportHeight -= EditorAssetBrowserLayout::AssetHeaderHeight;
    }
    const int assetContentHeight = AssetContentHeight(layout, state, manager);
    if (assetContentHeight > assetViewportHeight) {
        RECT track = EditorAssetBrowserLayout::AssetScrollbarTrackRect(layout);
        if (state.ViewMode() == EditorAssetViewMode::List) {
            track.top += EditorAssetBrowserLayout::AssetHeaderHeight;
        }
        const RECT thumb = EditorAssetBrowserLayout::ScrollbarThumbRect(track, assetViewportHeight, assetContentHeight, state.ContentScrollOffset());
        if (EditorAssetBrowserGeometry::Contains(thumb, x, y)) {
            return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::ContentScrollbarThumb };
        }
        if (EditorAssetBrowserGeometry::Contains(track, x, y)) {
            return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::ContentScrollbarTrack };
        }
    }
    return std::nullopt;
}

} // namespace

EditorAssetBrowserHit EditorAssetBrowserHitTester::HitTest(
    const RECT& content,
    int x,
    int y,
    const EditorAssetBrowserState& state,
    const kb::assets::AssetManager& manager,
    const RECT* overlayBounds) {
    const EditorAssetBrowserLayoutRects layout = EditorAssetBrowserLayout::Build(content, state.TreeWidth());
    if (const std::optional<EditorAssetBrowserHit> hit = EditorAssetBrowserOverlayHitTester::HitTestDeleteConfirm(content, x, y, state, manager, overlayBounds)) {
        return *hit;
    }

    if (const std::optional<EditorAssetBrowserHit> hit = EditorAssetBrowserOverlayHitTester::HitTestDropActionMenu(content, x, y, state)) {
        return *hit;
    }

    if (state.IsFilterMenuOpen()) {
        const RECT filterMenu = EditorAssetBrowserLayout::FilterMenuRect(layout);
        if (EditorAssetBrowserGeometry::Contains(EditorAssetBrowserLayout::FilterMenuItemRect(filterMenu, 0), x, y)) {
            return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::FilterFolder };
        }
        if (EditorAssetBrowserGeometry::Contains(EditorAssetBrowserLayout::FilterMenuItemRect(filterMenu, 1), x, y)) {
            return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::FilterTemplate };
        }
        if (EditorAssetBrowserGeometry::Contains(filterMenu, x, y)) {
            return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::DropActionBody };
        }
    }

    if (!EditorAssetBrowserGeometry::Contains(layout.frame, x, y)) {
        return {};
    }

    if (const std::optional<EditorAssetBrowserHit> hit = EditorAssetBrowserOverlayHitTester::HitTestContextMenu(content, x, y, state, manager)) {
        return *hit;
    }

    if (const std::optional<EditorAssetBrowserHit> hit = EditorAssetBrowserChromeHitTester::HitTest(layout, x, y, state)) {
        return *hit;
    }

    if (const std::optional<EditorAssetBrowserHit> hit = HitTestScrollbars(layout, x, y, state, manager)) {
        return *hit;
    }

    if (const std::optional<EditorAssetBrowserHit> hit = EditorAssetBrowserTreeHitTester::HitTest(layout, x, y, state, manager)) {
        return *hit;
    }

    if (const std::optional<EditorAssetBrowserHit> hit = EditorAssetBrowserContentHitTester::HitTest(layout, x, y, state, manager)) {
        return *hit;
    }

    if (EditorAssetBrowserGeometry::Contains(layout.assetView, x, y) || EditorAssetBrowserGeometry::Contains(layout.tree, x, y)) {
        return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::DropTarget };
    }
    return {};
}

std::optional<std::filesystem::path> EditorAssetBrowserHitTester::PrefabAssetAt(
    const RECT& content,
    int x,
    int y,
    const EditorAssetBrowserState& state,
    const kb::assets::AssetManager& manager) {
    return EditorAssetBrowserHitPayloadResolver::PrefabAssetAt(HitTest(content, x, y, state, manager), state, manager);
}

std::optional<kb::assets::AssetId> EditorAssetBrowserHitTester::AssetIdAt(
    const RECT& content,
    int x,
    int y,
    const EditorAssetBrowserState& state,
    const kb::assets::AssetManager& manager) {
    return EditorAssetBrowserHitPayloadResolver::AssetIdAt(HitTest(content, x, y, state, manager), state, manager);
}

std::optional<kb::assets::AssetMetadata> EditorAssetBrowserHitTester::AssetMetadataAt(
    const RECT& content,
    int x,
    int y,
    const EditorAssetBrowserState& state,
    const kb::assets::AssetManager& manager) {
    return EditorAssetBrowserHitPayloadResolver::AssetMetadataAt(HitTest(content, x, y, state, manager), state, manager);
}

std::optional<std::filesystem::path> EditorAssetBrowserHitTester::FolderAt(
    const RECT& content,
    int x,
    int y,
    const EditorAssetBrowserState& state,
    const kb::assets::AssetManager& manager) {
    return EditorAssetBrowserHitPayloadResolver::FolderAt(HitTest(content, x, y, state, manager), state, manager);
}

std::optional<std::filesystem::path> EditorAssetBrowserHitTester::FolderDropTargetAt(
    const RECT& content,
    int x,
    int y,
    const EditorAssetBrowserState& state,
    const kb::assets::AssetManager& manager) {
    return EditorAssetBrowserHitPayloadResolver::FolderDropTargetAt(HitTest(content, x, y, state, manager), state, manager);
}

std::optional<std::filesystem::path> EditorAssetBrowserHitTester::BreadcrumbFolderAt(
    const RECT& content,
    int x,
    int y,
    const EditorAssetBrowserState& state) {
    return EditorAssetBrowserBreadcrumbHitTester::FolderAt(EditorAssetBrowserLayout::Build(content, state.TreeWidth()), x, y, state);
}

bool EditorAssetBrowserHitTester::IsDropTarget(const RECT& content, int x, int y) noexcept {
    const EditorAssetBrowserLayoutRects layout = EditorAssetBrowserLayout::Build(content);
    return EditorAssetBrowserGeometry::Contains(layout.assetView, x, y) || EditorAssetBrowserGeometry::Contains(layout.tree, x, y);
}

} // namespace kb::editor

#endif
