#include "assets/EditorAssetBrowserBreadcrumbHitTester.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserGeometry.hpp"
#include "assets/EditorAssetBrowserState.hpp"
#include "engine/assets/AssetMetadata.hpp"

#include <string>
#include <vector>

namespace kb::editor {

std::optional<std::filesystem::path> EditorAssetBrowserBreadcrumbHitTester::FolderAt(
    const EditorAssetBrowserLayoutRects& layout,
    int x,
    int y,
    const EditorAssetBrowserState& state) {
    if (!EditorAssetBrowserGeometry::Contains(layout.path, x, y)) {
        return std::nullopt;
    }

    const std::vector<std::string> segments = EditorAssetBrowserGeometry::BreadcrumbSegments(state.SelectedFolder());
    std::filesystem::path path;
    int left = layout.path.left;
    for (std::size_t index = 0; index < segments.size(); ++index) {
        const std::string& segment = segments[index];
        path = path.empty() ? std::filesystem::path{ "/" + segment } : path / segment;

        const std::string label = EditorAssetBrowserGeometry::BreadcrumbDisplayLabel(segment, index);
        const int width = EditorAssetBrowserGeometry::BreadcrumbSegmentWidth(label, index == 0U);
        const RECT segmentRect{ left, layout.path.top, left + width, layout.path.bottom };
        if (EditorAssetBrowserGeometry::Contains(segmentRect, x, y)) {
            return std::filesystem::path{ kb::assets::NormalizeAssetPath(path) };
        }
        left += width + 10;
    }
    return std::nullopt;
}

} // namespace kb::editor

#endif
