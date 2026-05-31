#include "assets/EditorAssetBrowserTreeHitTester.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserGeometry.hpp"
#include "assets/EditorAssetBrowserState.hpp"

#include <vector>

namespace kb::editor {

std::optional<EditorAssetBrowserHit> EditorAssetBrowserTreeHitTester::HitTest(
    const EditorAssetBrowserLayoutRects& layout,
    int x,
    int y,
    const EditorAssetBrowserState& state,
    const kb::assets::AssetManager& manager) {
    const std::vector<EditorAssetFolderRow> folders = state.FolderRows(manager);
    for (std::size_t index = 0; index < folders.size(); ++index) {
        const RECT row = EditorAssetBrowserLayout::FolderRowRect(layout, static_cast<int>(index));
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
