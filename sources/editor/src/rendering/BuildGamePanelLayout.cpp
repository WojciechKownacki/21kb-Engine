#include "rendering/BuildGamePanelLayout.hpp"

#if defined(_WIN32)
#include <algorithm>
#include <numeric>

namespace kb::editor {
namespace {

constexpr int kRootInset = 8;
constexpr int kRootGap = 8;
constexpr int kSidebarWidth = 236;
constexpr int kHeaderHeight = 54;
constexpr int kFooterHeight = 50;
constexpr int kCaptionHeight = 24;
constexpr int kProfilesBlockRows = 2;
constexpr int kBuildButtonWidth = 176;
constexpr int kBuildButtonHeight = 34;
constexpr int kLabelColumnWidth = 596;
constexpr int kMinLabelColumnWidth = 150;
constexpr int kRowInsetX = panel_style::kRowPadX;
constexpr int kScrollbarWidth = 10;
constexpr int kScrollbarMinThumb = 28;

// Where the value field starts. The label column is the fixed width the panel was
// drawn at, until the row is too narrow to afford it - then it gives way rather than
// squeezing every value down to an ellipsis.
[[nodiscard]] int ValueColumnLeft(const RECT& row) noexcept {
    const int width = static_cast<int>(row.right - row.left);
    const int share = std::max(kMinLabelColumnWidth, (width * 45) / 100);
    return static_cast<int>(row.left) + std::min(kLabelColumnWidth, share);
}

} // namespace

BuildGamePanelLayoutRects BuildGamePanelLayout::Resolve(const RECT& content) noexcept {
    BuildGamePanelLayoutRects rects{};
    const int left = static_cast<int>(content.left) + kRootInset;
    const int top = static_cast<int>(content.top) + kRootInset;
    const int right = std::max(left, static_cast<int>(content.right) - kRootInset);
    const int bottom = std::max(top, static_cast<int>(content.bottom) - kRootInset);

    const int sidebarRight = std::min(right, left + kSidebarWidth);
    rects.sidebar = RECT{ left, top, sidebarRight, bottom };
    rects.divider = RECT{ sidebarRight, top, sidebarRight + 1, bottom };

    // Profiles sit at the foot of the sidebar; the target list takes whatever is above.
    const int profilesHeight = kCaptionHeight + (kProfilesBlockRows * kProfileRowHeight);
    const int profilesTop = std::max(top, bottom - profilesHeight);
    rects.profilesCaption = RECT{ left, profilesTop, sidebarRight, profilesTop + kCaptionHeight };
    rects.profilesList = RECT{ left, rects.profilesCaption.bottom, sidebarRight, bottom };
    rects.platformsCaption = RECT{ left, top, sidebarRight, top + kCaptionHeight };
    rects.platformsList = RECT{
        left,
        rects.platformsCaption.bottom,
        sidebarRight,
        std::max(static_cast<int>(rects.platformsCaption.bottom), profilesTop - kRootGap),
    };

    const int contentLeft = std::min(right, static_cast<int>(rects.divider.right) + kRootGap);
    rects.header = RECT{ contentLeft, top, right, std::min(bottom, top + kHeaderHeight) };
    const int footerTop = std::max(static_cast<int>(rects.header.bottom), bottom - kFooterHeight);
    rects.footer = RECT{ contentLeft, footerTop, right, bottom };
    rects.body = RECT{
        contentLeft,
        std::min(footerTop, static_cast<int>(rects.header.bottom) + kRootGap),
        right,
        footerTop,
    };

    rects.status = RECT{
        rects.footer.left + 4,
        rects.footer.top,
        std::max(static_cast<int>(rects.footer.left), static_cast<int>(rects.footer.right) - kBuildButtonWidth - kRootGap),
        rects.footer.bottom,
    };
    const int buttonTop = static_cast<int>(rects.footer.top) + ((kFooterHeight - kBuildButtonHeight) / 2);
    rects.buildButton = RECT{
        std::max(static_cast<int>(rects.footer.left), static_cast<int>(rects.footer.right) - kBuildButtonWidth),
        buttonTop,
        rects.footer.right,
        buttonTop + kBuildButtonHeight,
    };
    return rects;
}

RECT BuildGamePanelLayout::TargetRow(const RECT& list, int index) noexcept {
    // Player rows come first under one caption, then the server rows under a second.
    const int captionsAbove = index < 3 ? 1 : 2;
    const int top = static_cast<int>(list.top) + (captionsAbove * kGroupCaptionHeight) + (index * kTargetRowHeight);
    return RECT{ list.left, top, list.right, top + kTargetRowHeight };
}

RECT BuildGamePanelLayout::TargetGroupCaption(const RECT& list, int index) noexcept {
    const int rowsAbove = index == 0 ? 0 : 3;
    const int top = static_cast<int>(list.top) + (index * kGroupCaptionHeight) + (rowsAbove * kTargetRowHeight);
    return RECT{ list.left, top, list.right, top + kGroupCaptionHeight };
}

RECT BuildGamePanelLayout::ProfileRow(const RECT& list, int index) noexcept {
    const int top = static_cast<int>(list.top) + (index * kProfileRowHeight);
    return RECT{ list.left, top, list.right, top + kProfileRowHeight };
}

RECT BuildGamePanelLayout::SectionHeaderRow(const RECT& body, int y) noexcept {
    return RECT{ body.left, y, body.right, y + kSectionHeaderHeight };
}

RECT BuildGamePanelLayout::OptionRow(const RECT& body, int y) noexcept {
    return RECT{ body.left, y, body.right, y + kOptionRowHeight };
}

RECT BuildGamePanelLayout::OptionLabel(const RECT& row) noexcept {
    return RECT{
        row.left + kRowInsetX,
        row.top,
        std::max(static_cast<int>(row.left), ValueColumnLeft(row) - kRowInsetX),
        row.bottom,
    };
}

RECT BuildGamePanelLayout::OptionValueBox(const RECT& row) noexcept {
    const int boxLeft = std::min(static_cast<int>(row.right), ValueColumnLeft(row));
    const int height = panel_style::kValueHeight;
    const int top = static_cast<int>(row.top) + ((static_cast<int>(row.bottom - row.top) - height) / 2);
    return RECT{ boxLeft, top,
        std::max(boxLeft, static_cast<int>(row.right) - panel_style::kRowPadX), top + height };
}

RECT BuildGamePanelLayout::CaretBox(const RECT& header) noexcept {
    constexpr int size = 12;
    const int top = static_cast<int>(header.top) + ((static_cast<int>(header.bottom - header.top) - size) / 2);
    return RECT{ header.left + kRowInsetX, top, header.left + kRowInsetX + size, top + size };
}

RECT BuildGamePanelLayout::IconBox(const RECT& row, int size) noexcept {
    const int top = static_cast<int>(row.top) + ((static_cast<int>(row.bottom - row.top) - size) / 2);
    return RECT{ row.left + kRowInsetX, top, row.left + kRowInsetX + size, top + size };
}

int BuildGamePanelLayout::ContentHeight(std::span<const int> sectionRowCounts) noexcept {
    int height = 0;
    for (const int rows : sectionRowCounts) {
        height += kSectionHeaderHeight + (rows * kOptionRowHeight) + kSectionSpacing;
    }
    return height;
}

int BuildGamePanelLayout::MaxScrollOffset(const RECT& body, int contentHeight) noexcept {
    return std::max(0, contentHeight - static_cast<int>(body.bottom - body.top));
}

RECT BuildGamePanelLayout::ScrollbarTrack(const RECT& body, int contentHeight) noexcept {
    if (MaxScrollOffset(body, contentHeight) <= 0) {
        return RECT{};
    }
    return RECT{ std::max(static_cast<int>(body.left), static_cast<int>(body.right) - kScrollbarWidth),
        body.top, body.right, body.bottom };
}

RECT BuildGamePanelLayout::ScrollbarThumb(const RECT& body, int contentHeight, int scrollOffset) noexcept {
    const RECT track = ScrollbarTrack(body, contentHeight);
    const int trackHeight = static_cast<int>(track.bottom - track.top);
    if (trackHeight <= 0 || contentHeight <= 0) {
        return RECT{};
    }
    const int maxScroll = MaxScrollOffset(body, contentHeight);
    const int thumbHeight = std::max(kScrollbarMinThumb, (trackHeight * trackHeight) / contentHeight);
    const int travel = std::max(0, trackHeight - thumbHeight);
    const int offset = maxScroll <= 0 ? 0 : (travel * std::clamp(scrollOffset, 0, maxScroll)) / maxScroll;
    return RECT{ track.left, track.top + offset, track.right, track.top + offset + thumbHeight };
}

} // namespace kb::editor

#endif
