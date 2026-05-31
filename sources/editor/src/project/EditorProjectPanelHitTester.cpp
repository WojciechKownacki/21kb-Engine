#include "project/EditorProjectPanelHitTester.hpp"

#if defined(_WIN32)
#include "project/EditorProjectAssetIndex.hpp"

namespace kb::editor {
namespace {

[[nodiscard]] bool Contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] RECT FilesRect(RECT content) noexcept {
    RECT frame{ content.left + 8, content.top + 8, content.right - 8, content.bottom - 8 };
    const int treeWidth = (frame.right - frame.left) / 3;
    frame.left += treeWidth + 8;
    return frame;
}

} // namespace

bool EditorProjectPanelHitTester::IsPrefabDropTarget(const RECT& content, int x, int y) noexcept {
    return Contains(content, x, y);
}

std::optional<std::filesystem::path> EditorProjectPanelHitTester::PrefabAssetAt(const RECT& content, int x, int y) {
    const RECT files = FilesRect(content);
    if (!Contains(files, x, y)) {
        return std::nullopt;
    }

    const int rowHeight = 24;
    const int firstAssetRow = 2;
    const int row = (y - (files.top + 10)) / rowHeight;
    const int assetIndex = row - firstAssetRow;
    const std::vector<std::filesystem::path> assets = EditorProjectAssetIndex::PrefabAssets();
    if (assetIndex < 0 || static_cast<std::size_t>(assetIndex) >= assets.size()) {
        return std::nullopt;
    }
    return assets[static_cast<std::size_t>(assetIndex)];
}

} // namespace kb::editor

#endif
