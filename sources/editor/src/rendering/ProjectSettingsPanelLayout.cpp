#include "rendering/ProjectSettingsPanelLayout.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"

namespace kb::editor {
namespace {

constexpr int kPadding = 16;
constexpr int kHeaderHeight = 46;
constexpr int kSidebarWidth = 190;
constexpr int kCategoryTopInset = 8;
constexpr int kCategoryRowHeight = 30;
constexpr int kSectionHeaderHeight = 26;
constexpr int kRowHeight = 34;
constexpr int kRowGap = 6;
constexpr int kLabelWidth = 150;
constexpr int kCheckboxSize = 18;
constexpr int kOptionHeight = 28;
constexpr int kBackendButtonWidth = 86;

} // namespace

ProjectSettingsPanelLayoutRects ProjectSettingsPanelLayout::Resolve(const RECT& content) noexcept {
    ProjectSettingsPanelLayoutRects rects{};
    rects.header = RECT{ content.left, content.top, content.right, content.top + kHeaderHeight };

    rects.sidebar = RECT{ content.left, rects.header.bottom, content.left + kSidebarWidth, content.bottom };
    rects.divider = RECT{ rects.sidebar.right, rects.header.bottom, rects.sidebar.right + 1, content.bottom };
    rects.content = RECT{ rects.divider.right, rects.header.bottom, content.right, content.bottom };

    // Inputs page content, laid out relative to the right pane.
    const int left = rects.content.left + kPadding;
    const int right = rects.content.right - kPadding;
    const int sectionTop = rects.content.top + kPadding;
    rects.sectionHeader = RECT{ left, sectionTop, right, sectionTop + kSectionHeaderHeight };

    const int row1Top = rects.sectionHeader.bottom + kRowGap;
    rects.mappingLabel = RECT{ left, row1Top, left + kLabelWidth, row1Top + kRowHeight };
    rects.mappingField = RECT{ left + kLabelWidth, row1Top, right, row1Top + kRowHeight };

    const int row2Top = row1Top + kRowHeight + kRowGap;
    rects.enabledLabel = RECT{ left, row2Top, left + kLabelWidth, row2Top + kRowHeight };
    const int checkTop = row2Top + (kRowHeight - kCheckboxSize) / 2;
    rects.enabledCheckbox = RECT{ left + kLabelWidth, checkTop, left + kLabelWidth + kCheckboxSize, checkTop + kCheckboxSize };

    const int graphicsRowTop = rects.sectionHeader.bottom + kRowGap;
    rects.backendLabel = RECT{ left, graphicsRowTop, left + kLabelWidth, graphicsRowTop + kRowHeight };
    rects.backendAutoButton = RECT{ left + kLabelWidth, graphicsRowTop + 3, left + kLabelWidth + kBackendButtonWidth, graphicsRowTop + kRowHeight - 3 };
    rects.backendDx12Button = RECT{ rects.backendAutoButton.right + 6, graphicsRowTop + 3, rects.backendAutoButton.right + 6 + kBackendButtonWidth, graphicsRowTop + kRowHeight - 3 };
    rects.backendVulkanButton = RECT{ rects.backendDx12Button.right + 6, graphicsRowTop + 3, rects.backendDx12Button.right + 6 + kBackendButtonWidth, graphicsRowTop + kRowHeight - 3 };
    return rects;
}

RECT ProjectSettingsPanelLayout::CategoryRow(const RECT& sidebar, int index) noexcept {
    const int top = sidebar.top + kCategoryTopInset + (index * kCategoryRowHeight);
    return RECT{ sidebar.left + 6, top, sidebar.right - 6, top + kCategoryRowHeight };
}

RECT ProjectSettingsPanelLayout::MappingFieldBox(const ProjectSettingsPanelLayoutRects& rects) noexcept {
    return GdiDrawing::Inset(rects.mappingField, 4);
}

RECT ProjectSettingsPanelLayout::OptionRow(const RECT& fieldBox, int index) noexcept {
    const int top = fieldBox.bottom + (index * kOptionHeight);
    return RECT{ fieldBox.left, top, fieldBox.right, top + kOptionHeight };
}

RECT ProjectSettingsPanelLayout::OptionListBounds(const RECT& fieldBox, int count) noexcept {
    return RECT{ fieldBox.left, fieldBox.bottom, fieldBox.right, fieldBox.bottom + (count * kOptionHeight) };
}

RECT ProjectSettingsPanelLayout::BackendOptionButton(const ProjectSettingsPanelLayoutRects& rects, int index) noexcept {
    switch (index) {
    case 0:
        return rects.backendAutoButton;
    case 1:
        return rects.backendDx12Button;
    case 2:
        return rects.backendVulkanButton;
    default:
        return {};
    }
}

} // namespace kb::editor

#endif
