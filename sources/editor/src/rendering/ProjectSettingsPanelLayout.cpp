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
constexpr int kMsaaButtonWidth = 58;
constexpr int kAaButtonWidth = 64;

[[nodiscard]] int RowTopAfter(const RECT& row) noexcept {
    return row.bottom + kRowGap;
}

[[nodiscard]] RECT CheckboxForRow(int left, int rowTop) noexcept {
    return RECT{ left + kLabelWidth, rowTop + (kRowHeight - kCheckboxSize) / 2, left + kLabelWidth + kCheckboxSize, rowTop + (kRowHeight + kCheckboxSize) / 2 };
}

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

    const int lightingPathRowTop = graphicsRowTop + kRowHeight + kRowGap;
    rects.lightingPathLabel = RECT{ left, lightingPathRowTop, left + kLabelWidth, lightingPathRowTop + kRowHeight };
    rects.lightingPathForwardButton = RECT{ left + kLabelWidth, lightingPathRowTop + 3, left + kLabelWidth + kBackendButtonWidth, lightingPathRowTop + kRowHeight - 3 };
    rects.lightingPathDeferredButton = RECT{ rects.lightingPathForwardButton.right + 6, lightingPathRowTop + 3, rects.lightingPathForwardButton.right + 6 + kBackendButtonWidth, lightingPathRowTop + kRowHeight - 3 };

    const int postRowTop = lightingPathRowTop + kRowHeight + kRowGap;
    rects.postProcessLabel = RECT{ left, postRowTop, left + kLabelWidth, postRowTop + kRowHeight };
    rects.postProcessCheckbox = CheckboxForRow(left, postRowTop);

    const int aaRowTop = RowTopAfter(rects.postProcessLabel);
    rects.antiAliasingLabel = RECT{ left, aaRowTop, left + kLabelWidth, aaRowTop + kRowHeight };
    rects.antiAliasingNoneButton = RECT{ left + kLabelWidth, aaRowTop + 3, left + kLabelWidth + kAaButtonWidth, aaRowTop + kRowHeight - 3 };
    rects.antiAliasingFxaaButton = RECT{ rects.antiAliasingNoneButton.right + 6, aaRowTop + 3, rects.antiAliasingNoneButton.right + 6 + kAaButtonWidth, aaRowTop + kRowHeight - 3 };
    rects.antiAliasingTaaButton = RECT{ rects.antiAliasingFxaaButton.right + 6, aaRowTop + 3, rects.antiAliasingFxaaButton.right + 6 + kAaButtonWidth, aaRowTop + kRowHeight - 3 };
    rects.antiAliasingMsaaButton = RECT{ rects.antiAliasingTaaButton.right + 6, aaRowTop + 3, rects.antiAliasingTaaButton.right + 6 + kAaButtonWidth, aaRowTop + kRowHeight - 3 };

    const int msaaRowTop = RowTopAfter(rects.antiAliasingLabel);
    rects.msaaLabel = RECT{ left, msaaRowTop, left + kLabelWidth, msaaRowTop + kRowHeight };
    rects.msaaOffButton = RECT{ left + kLabelWidth, msaaRowTop + 3, left + kLabelWidth + kMsaaButtonWidth, msaaRowTop + kRowHeight - 3 };
    rects.msaa2xButton = RECT{ rects.msaaOffButton.right + 6, msaaRowTop + 3, rects.msaaOffButton.right + 6 + kMsaaButtonWidth, msaaRowTop + kRowHeight - 3 };
    rects.msaa4xButton = RECT{ rects.msaa2xButton.right + 6, msaaRowTop + 3, rects.msaa2xButton.right + 6 + kMsaaButtonWidth, msaaRowTop + kRowHeight - 3 };

    const int bloomRowTop = RowTopAfter(rects.msaaLabel);
    rects.bloomLabel = RECT{ left, bloomRowTop, left + kLabelWidth, bloomRowTop + kRowHeight };
    rects.bloomCheckbox = CheckboxForRow(left, bloomRowTop);

    const int shadowsRowTop = RowTopAfter(rects.bloomLabel);
    rects.shadowsLabel = RECT{ left, shadowsRowTop, left + kLabelWidth, shadowsRowTop + kRowHeight };
    rects.shadowsCheckbox = CheckboxForRow(left, shadowsRowTop);

    const int outlineRowTop = RowTopAfter(rects.shadowsLabel);
    rects.selectionOutlineLabel = RECT{ left, outlineRowTop, left + kLabelWidth, outlineRowTop + kRowHeight };
    rects.selectionOutlineCheckbox = CheckboxForRow(left, outlineRowTop);

    const int gpuRowTop = RowTopAfter(rects.selectionOutlineLabel);
    rects.gpuDrivenLabel = RECT{ left, gpuRowTop, left + kLabelWidth, gpuRowTop + kRowHeight };
    rects.gpuDrivenCheckbox = CheckboxForRow(left, gpuRowTop);
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

RECT ProjectSettingsPanelLayout::LightingPathOptionButton(const ProjectSettingsPanelLayoutRects& rects, int index) noexcept {
    switch (index) {
    case 0:
        return rects.lightingPathForwardButton;
    case 1:
        return rects.lightingPathDeferredButton;
    default:
        return {};
    }
}

RECT ProjectSettingsPanelLayout::GraphicsToggleCheckbox(const ProjectSettingsPanelLayoutRects& rects, int index) noexcept {
    switch (index) {
    case 0:
        return rects.postProcessCheckbox;
    case 1:
        return rects.bloomCheckbox;
    case 2:
        return rects.shadowsCheckbox;
    case 3:
        return rects.selectionOutlineCheckbox;
    case 4:
        return rects.gpuDrivenCheckbox;
    default:
        return {};
    }
}

RECT ProjectSettingsPanelLayout::GraphicsToggleLabel(const ProjectSettingsPanelLayoutRects& rects, int index) noexcept {
    switch (index) {
    case 0:
        return rects.postProcessLabel;
    case 1:
        return rects.bloomLabel;
    case 2:
        return rects.shadowsLabel;
    case 3:
        return rects.selectionOutlineLabel;
    case 4:
        return rects.gpuDrivenLabel;
    default:
        return {};
    }
}

RECT ProjectSettingsPanelLayout::AntiAliasingModeButton(const ProjectSettingsPanelLayoutRects& rects, int index) noexcept {
    switch (index) {
    case 0:
        return rects.antiAliasingNoneButton;
    case 1:
        return rects.antiAliasingFxaaButton;
    case 2:
        return rects.antiAliasingTaaButton;
    case 3:
        return rects.antiAliasingMsaaButton;
    default:
        return {};
    }
}

RECT ProjectSettingsPanelLayout::MsaaOptionButton(const ProjectSettingsPanelLayoutRects& rects, int index) noexcept {
    switch (index) {
    case 0:
        return rects.msaaOffButton;
    case 1:
        return rects.msaa2xButton;
    case 2:
        return rects.msaa4xButton;
    default:
        return {};
    }
}

} // namespace kb::editor

#endif
