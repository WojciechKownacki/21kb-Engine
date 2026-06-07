#include "assets/EditorAssetBrowserGeometry.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserState.hpp"
#include "engine/assets/AssetMetadata.hpp"

#include <algorithm>

namespace kb::editor {
namespace {

constexpr int kDeleteConfirmListHeaderHeight = 26;
constexpr int kDeleteConfirmListRowHeight = 24;
constexpr int kDeleteConfirmListScrollbarWidth = 12;
constexpr int kDeleteConfirmListCheckboxColumnWidth = 32;
constexpr int kDeleteConfirmListCheckboxSize = 15;

[[nodiscard]] int Height(const RECT& rect) noexcept {
    return static_cast<int>(rect.bottom - rect.top);
}

} // namespace

bool EditorAssetBrowserGeometry::Contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

RECT EditorAssetBrowserGeometry::DeleteConfirmRect(const RECT& bounds, int offsetX, int offsetY) noexcept {
    constexpr int preferredWidth = 520;
    constexpr int preferredHeight = 340;
    constexpr int margin = 12;
    const int boundsWidth = std::max(1, static_cast<int>(bounds.right - bounds.left));
    const int boundsHeight = std::max(1, static_cast<int>(bounds.bottom - bounds.top));
    const int width = std::clamp(preferredWidth, 320, std::max(320, boundsWidth - margin * 2));
    const int height = std::clamp(preferredHeight, 240, std::max(240, boundsHeight - margin * 2));
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
    return RECT{ dialog.right - 216, dialog.bottom - 44, dialog.right - 116, dialog.bottom - 14 };
}

RECT EditorAssetBrowserGeometry::DeleteConfirmCancelRect(const RECT& dialog) noexcept {
    return RECT{ dialog.right - 104, dialog.bottom - 44, dialog.right - 22, dialog.bottom - 14 };
}

RECT EditorAssetBrowserGeometry::DeleteConfirmListRect(const RECT& bounds, const EditorAssetBrowserState& state) noexcept {
    const RECT dialog = DeleteConfirmRect(bounds, state.DeleteConfirmOffsetX(), state.DeleteConfirmOffsetY());
    return RECT{ dialog.left + 24, dialog.top + 108, dialog.right - 24, dialog.bottom - 62 };
}

RECT EditorAssetBrowserGeometry::DeleteConfirmListViewportRect(const RECT& bounds, const EditorAssetBrowserState& state) noexcept {
    const RECT list = DeleteConfirmListRect(bounds, state);
    return RECT{
        list.left + 1,
        list.top + kDeleteConfirmListHeaderHeight + 2,
        list.right - 1,
        list.bottom - 1,
    };
}

RECT EditorAssetBrowserGeometry::DeleteConfirmListScrollbarTrackRect(const RECT& bounds, const EditorAssetBrowserState& state) noexcept {
    const RECT viewport = DeleteConfirmListViewportRect(bounds, state);
    return RECT{ viewport.right - kDeleteConfirmListScrollbarWidth, viewport.top, viewport.right, viewport.bottom };
}

RECT EditorAssetBrowserGeometry::DeleteConfirmListScrollbarThumbRect(const RECT& bounds, const EditorAssetBrowserState& state, const kb::assets::AssetManager& manager) {
    const RECT track = DeleteConfirmListScrollbarTrackRect(bounds, state);
    const int viewportHeight = Height(track);
    const int contentHeight = static_cast<int>(state.DeleteTargetRows(manager).size()) * kDeleteConfirmListRowHeight;
    if (contentHeight <= viewportHeight || viewportHeight <= 0) {
        return RECT{};
    }
    const int thumbHeight = std::max(18, viewportHeight * viewportHeight / std::max(1, contentHeight));
    const int travel = std::max(1, viewportHeight - thumbHeight);
    const int maxScroll = std::max(1, contentHeight - viewportHeight);
    const int scroll = std::clamp(state.DeleteConfirmListScrollOffset(), 0, maxScroll);
    const int thumbTop = track.top + (scroll * travel) / maxScroll;
    return RECT{ track.left + 2, thumbTop + 2, track.right - 2, thumbTop + thumbHeight - 2 };
}

RECT EditorAssetBrowserGeometry::DeleteConfirmListCheckboxRect(const RECT& bounds, const EditorAssetBrowserState& state, std::size_t rowIndex) noexcept {
    const RECT viewport = DeleteConfirmListViewportRect(bounds, state);
    const int rowTop = viewport.top + static_cast<int>(rowIndex) * kDeleteConfirmListRowHeight - state.DeleteConfirmListScrollOffset();
    const int boxLeft = viewport.left + (kDeleteConfirmListCheckboxColumnWidth - kDeleteConfirmListCheckboxSize) / 2;
    const int boxTop = rowTop + (kDeleteConfirmListRowHeight - kDeleteConfirmListCheckboxSize) / 2;
    return RECT{ boxLeft, boxTop, boxLeft + kDeleteConfirmListCheckboxSize, boxTop + kDeleteConfirmListCheckboxSize };
}

std::optional<std::size_t> EditorAssetBrowserGeometry::DeleteConfirmListRowAt(
    const RECT& bounds,
    const EditorAssetBrowserState& state,
    const kb::assets::AssetManager& manager,
    int x,
    int y) {
    const RECT viewport = DeleteConfirmListViewportRect(bounds, state);
    if (!Contains(RECT{ viewport.left, viewport.top, viewport.right - kDeleteConfirmListScrollbarWidth, viewport.bottom }, x, y)) {
        return std::nullopt;
    }
    const int relativeY = y - viewport.top + state.DeleteConfirmListScrollOffset();
    if (relativeY < 0) {
        return std::nullopt;
    }
    const std::size_t rowIndex = static_cast<std::size_t>(relativeY / kDeleteConfirmListRowHeight);
    return rowIndex < state.DeleteTargetRows(manager).size() ? std::optional<std::size_t>{ rowIndex } : std::nullopt;
}

int EditorAssetBrowserGeometry::DeleteConfirmListMaxScroll(const RECT& bounds, const EditorAssetBrowserState& state, const kb::assets::AssetManager& manager) {
    const RECT viewport = DeleteConfirmListViewportRect(bounds, state);
    const int contentHeight = static_cast<int>(state.DeleteTargetRows(manager).size()) * kDeleteConfirmListRowHeight;
    return std::max(0, contentHeight - Height(viewport));
}

int EditorAssetBrowserGeometry::DeleteConfirmListRowHeight() noexcept {
    return kDeleteConfirmListRowHeight;
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
