#include "assets/EditorAssetBrowserGeometry.hpp"

#if defined(_WIN32)
#include "engine/assets/AssetMetadata.hpp"

#include <algorithm>

namespace kb::editor {

bool EditorAssetBrowserGeometry::Contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

RECT EditorAssetBrowserGeometry::DeleteConfirmRect(const RECT& bounds, int offsetX, int offsetY) noexcept {
    constexpr int preferredWidth = 460;
    constexpr int preferredHeight = 196;
    constexpr int margin = 12;
    const int boundsWidth = std::max(1, static_cast<int>(bounds.right - bounds.left));
    const int boundsHeight = std::max(1, static_cast<int>(bounds.bottom - bounds.top));
    const int width = std::clamp(preferredWidth, 320, std::max(320, boundsWidth - margin * 2));
    const int height = std::clamp(preferredHeight, 156, std::max(156, boundsHeight - margin * 2));
    const int minLeft = bounds.left + margin;
    const int maxLeft = std::max(minLeft, static_cast<int>(bounds.right) - margin - width);
    const int minTop = bounds.top + margin;
    const int maxTop = std::max(minTop, static_cast<int>(bounds.bottom) - margin - height);
    const int centeredLeft = bounds.left + (boundsWidth - width) / 2 + offsetX;
    const int centeredTop = bounds.top + (boundsHeight - height) / 2 + offsetY;
    const int left = std::clamp(centeredLeft, minLeft, maxLeft);
    const int top = std::clamp(centeredTop, minTop, maxTop);
    return RECT{ left, top, left + width, top + height };
}

RECT EditorAssetBrowserGeometry::DeleteConfirmAcceptRect(const RECT& dialog) noexcept {
    return RECT{ dialog.right - 216, dialog.bottom - 52, dialog.right - 116, dialog.bottom - 22 };
}

RECT EditorAssetBrowserGeometry::DeleteConfirmCancelRect(const RECT& dialog) noexcept {
    return RECT{ dialog.right - 104, dialog.bottom - 52, dialog.right - 22, dialog.bottom - 22 };
}

RECT EditorAssetBrowserGeometry::FolderDisclosureRect(RECT row, const EditorAssetFolderRow& folder) noexcept {
    const int indent = folder.depth * 14;
    return RECT{ row.left + indent, row.top, row.left + indent + 16, row.bottom };
}

RECT EditorAssetBrowserGeometry::SliderHitRect(const EditorAssetBrowserLayoutRects& layout) noexcept {
    RECT sliderHit = layout.sliderTrack;
    sliderHit.left -= 8;
    sliderHit.right += 8;
    sliderHit.top -= 8;
    sliderHit.bottom += 8;
    return sliderHit;
}

float EditorAssetBrowserGeometry::SliderValueAt(const EditorAssetBrowserLayoutRects& layout, int x) noexcept {
    const int width = std::max(1, static_cast<int>(layout.sliderTrack.right - layout.sliderTrack.left));
    const float t = std::clamp(static_cast<float>(x - layout.sliderTrack.left) / static_cast<float>(width), 0.0F, 1.0F);
    return 0.65F + t * 1.10F;
}

std::vector<std::string> EditorAssetBrowserGeometry::BreadcrumbSegments(const std::filesystem::path& folder) {
    const std::string normalized = kb::assets::NormalizeAssetPath(folder);
    std::vector<std::string> segments;
    std::size_t start = normalized.front() == '/' ? 1U : 0U;
    while (start < normalized.size()) {
        const std::size_t separator = normalized.find('/', start);
        const std::size_t end = separator == std::string::npos ? normalized.size() : separator;
        segments.push_back(normalized.substr(start, end - start));
        if (separator == std::string::npos) {
            break;
        }
        start = separator + 1;
    }
    return segments;
}

std::string EditorAssetBrowserGeometry::BreadcrumbDisplayLabel(const std::string& segment, std::size_t index) {
    if (index == 0U && segment == "Game") {
        return "Content";
    }
    return segment;
}

int EditorAssetBrowserGeometry::BreadcrumbSegmentWidth(std::string_view label, bool root) noexcept {
    const int iconSpace = root ? 32 : 20;
    return std::clamp(static_cast<int>(label.size()) * 7 + iconSpace, root ? 86 : 42, root ? 132 : 160);
}

} // namespace kb::editor

#endif
