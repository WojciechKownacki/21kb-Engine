#include "assets/EditorAssetBrowserTreeHitTester.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserGeometry.hpp"
#include "assets/EditorAssetBrowserState.hpp"

#include <algorithm>
#include <vector>

namespace kb::editor {

std::optional<EditorAssetBrowserHit> EditorAssetBrowserTreeHitTester::HitTest(
    const EditorAssetBrowserLayoutRects& layout,
    int x,
    int y,
    const EditorAssetBrowserState& state,
    const kb::assets::AssetManager& manager) {
    const std::vector<EditorAssetFolderRow> folders = state.FolderRows(manager);
    const RECT viewport = EditorAssetBrowserLayout::TreeViewportRect(layout);
    if (!EditorAssetBrowserGeometry::Contains(viewport, x, y)) {
        return std::nullopt;
    }
    const int firstRow = std::max(0, state.TreeScrollOffset() / EditorAssetBrowserLayout::RowHeight);
    const int visibleRows = (static_cast<int>(viewport.bottom - viewport.top) / EditorAssetBrowserLayout::RowHeight) + 3;
    const int lastRow = std::clamp(firstRow + visibleRows, 0, static_cast<int>(folders.size()));
    for (int rowIndex = firstRow; rowIndex < lastRow; ++rowIndex) {
        const std::size_t index = static_cast<std::size_t>(rowIndex);
        RECT row = EditorAssetBrowserLayout::FolderRowRect(layout, rowIndex);
        OffsetRect(&row, 0, -state.TreeScrollOffset());
        if (folders[index].hasChildren && EditorAssetBrowserGeometry::Contains(EditorAssetBrowserGeometry::FolderDisclosureRect(row, folders[index]), x, y)) {
            return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::FolderDisclosure, .index = index };
        }
        if (EditorAssetBrowserGeometry::Contains(row, x, y)) {
            return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::Folder, .index = index };
        }
    }
    return std::nullopt;
}

} // namespace kb::editor

#endif
