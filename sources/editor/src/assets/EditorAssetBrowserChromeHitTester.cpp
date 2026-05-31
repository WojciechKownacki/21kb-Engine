#include "assets/EditorAssetBrowserChromeHitTester.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserGeometry.hpp"
#include "assets/EditorAssetBrowserState.hpp"

namespace kb::editor {

std::optional<EditorAssetBrowserHit> EditorAssetBrowserChromeHitTester::HitTest(
    const EditorAssetBrowserLayoutRects& layout,
    int x,
    int y,
    const EditorAssetBrowserState& state) {
    if (EditorAssetBrowserGeometry::Contains(layout.search, x, y)) {
        return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::Search };
    }
    if (EditorAssetBrowserGeometry::Contains(layout.filtersButton, x, y)) {
        return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::Filters };
    }
    if (EditorAssetBrowserGeometry::Contains(layout.path, x, y)) {
        return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::Breadcrumb };
    }
    if (EditorAssetBrowserGeometry::Contains(layout.refreshButton, x, y)) {
        return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::Refresh };
    }
    if (EditorAssetBrowserGeometry::Contains(layout.newFolderButton, x, y)) {
        return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::NewFolder };
    }
    if (EditorAssetBrowserGeometry::Contains(layout.renameButton, x, y)) {
        return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::Rename };
    }
    if (EditorAssetBrowserGeometry::Contains(layout.deleteButton, x, y)) {
        return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::DeleteAsset };
    }
    if (state.IsSortMenuOpen() && EditorAssetBrowserGeometry::Contains(layout.sortNameItem, x, y)) {
        return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::SortByName };
    }
    if (state.IsSortMenuOpen() && EditorAssetBrowserGeometry::Contains(layout.sortTypeItem, x, y)) {
        return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::SortByType };
    }
    if (state.IsSortMenuOpen() && EditorAssetBrowserGeometry::Contains(layout.sortPathItem, x, y)) {
        return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::SortByPath };
    }
    if (EditorAssetBrowserGeometry::Contains(layout.listButton, x, y)) {
        return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::ListMode };
    }
    if (EditorAssetBrowserGeometry::Contains(layout.tileButton, x, y)) {
        return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::TileMode };
    }
    if (EditorAssetBrowserGeometry::Contains(layout.recursiveButton, x, y)) {
        return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::Recursive };
    }
    if (EditorAssetBrowserGeometry::Contains(layout.sortButton, x, y)) {
        return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::Sort };
    }
    if (EditorAssetBrowserGeometry::Contains(EditorAssetBrowserGeometry::SliderHitRect(layout), x, y)) {
        return EditorAssetBrowserHit{
            .kind = EditorAssetBrowserHitKind::Slider,
            .value = EditorAssetBrowserGeometry::SliderValueAt(layout, x),
        };
    }
    return std::nullopt;
}

} // namespace kb::editor

#endif
