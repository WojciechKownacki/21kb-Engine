#include "rendering/ProjectSettingsPanelLayout.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"

#include <algorithm>
#include <array>

namespace kb::editor {
namespace {

constexpr int kPadding = 16;
constexpr int kHeaderHeight = 46;
constexpr int kSidebarWidth = 190;
constexpr int kSearchInset = 8;
constexpr int kSearchHeight = 28;
// Categories start below the search box.
constexpr int kCategoryTopInset = kSearchInset + kSearchHeight + 6;
constexpr int kCategoryRowHeight = 30;
constexpr int kGroupCaptionHeight = 24;
// Every setting carries a one-line description under its row.
constexpr int kDescriptionHeight = 16;
constexpr int kSectionHeaderHeight = 26;
constexpr int kRowHeight = 34;
constexpr int kRowGap = 6;
constexpr int kLabelWidth = 150;
// The on/off toggles are switches: wider than tall.
constexpr int kCheckboxSize = 16;
constexpr int kCheckboxWidth = 30;
constexpr int kOptionHeight = 28;
constexpr int kBackendButtonWidth = 86;
constexpr int kMsaaButtonWidth = 58;
constexpr int kAaButtonWidth = 64;
constexpr int kQualityLabelWidth = 232;
constexpr int kQualityButtonWidth = 72;
constexpr int kQualityButtonGap = 6;
constexpr int kButtonGap = 6;
constexpr int kModeButtonWidth = 110;
constexpr int kSizeButtonWidth = 88;
constexpr int kRateButtonWidth = 56;
constexpr int kNumberCellWidth = 64;
constexpr int kSceneAddButtonWidth = 180;
constexpr int kTagNameFieldWidth = 240;
constexpr int kSceneUpButtonWidth = 100;
constexpr int kSceneRemoveButtonWidth = 110;
constexpr int kSceneRowPitch = kRowHeight + 4;
constexpr int kStepperButtonWidth = 32;
constexpr int kStepperValueWidth = 84;
constexpr int kGravityPresetButtonWidth = 110;

[[nodiscard]] int RowTopAfter(const RECT& row) noexcept {
    return row.bottom + kDescriptionHeight + kRowGap;
}

// How many group captions sit above the category at `index`.
[[nodiscard]] int CaptionsAbove(int index) noexcept {
    return 1 + (index >= ProjectSettingsPanelLayout::kEditorGroupStart ? 1 : 0) +
        (index >= ProjectSettingsPanelLayout::kPluginsGroupStart ? 1 : 0);
}

[[nodiscard]] RECT CheckboxForRow(int left, int rowTop) noexcept {
    return RECT{ left + kLabelWidth, rowTop + (kRowHeight - kCheckboxSize) / 2, left + kLabelWidth + kCheckboxWidth, rowTop + (kRowHeight + kCheckboxSize) / 2 };
}

} // namespace

ProjectSettingsPanelLayoutRects ProjectSettingsPanelLayout::Resolve(const RECT& content) noexcept {
    ProjectSettingsPanelLayoutRects rects{};
    rects.header = RECT{ content.left, content.top, content.right, content.top + kHeaderHeight };

    rects.sidebar = RECT{ content.left, rects.header.bottom, content.left + kSidebarWidth, content.bottom };
    rects.divider = RECT{ rects.sidebar.right, rects.header.bottom, rects.sidebar.right + 1, content.bottom };
    rects.searchField = RECT{ rects.sidebar.left + kSearchInset, rects.sidebar.top + kSearchInset,
        rects.sidebar.right - kSearchInset, rects.sidebar.top + kSearchInset + kSearchHeight };
    rects.content = RECT{ rects.divider.right, rects.header.bottom, content.right, content.bottom };

    // Inputs page content, laid out relative to the right pane.
    const int left = rects.content.left + kPadding;
    const int right = rects.content.right - kPadding;
    const int sectionTop = rects.content.top + kPadding;
    rects.sectionHeader = RECT{ left, sectionTop, right, sectionTop + kSectionHeaderHeight };

    const int row1Top = rects.sectionHeader.bottom + kRowGap;
    rects.mappingLabel = RECT{ left, row1Top, left + kLabelWidth, row1Top + kRowHeight };
    rects.mappingField = RECT{ left + kLabelWidth, row1Top, right, row1Top + kRowHeight };

    const int row2Top = RowTopAfter(rects.mappingLabel);
    rects.enabledLabel = RECT{ left, row2Top, left + kLabelWidth, row2Top + kRowHeight };
    const int checkTop = row2Top + (kRowHeight - kCheckboxSize) / 2;
    rects.enabledCheckbox = RECT{ left + kLabelWidth, checkTop, left + kLabelWidth + kCheckboxWidth, checkTop + kCheckboxSize };

    const int graphicsRowTop = rects.sectionHeader.bottom + kRowGap;
    rects.backendLabel = RECT{ left, graphicsRowTop, left + kLabelWidth, graphicsRowTop + kRowHeight };
    rects.backendAutoButton = RECT{ left + kLabelWidth, graphicsRowTop + 3, left + kLabelWidth + kBackendButtonWidth, graphicsRowTop + kRowHeight - 3 };
    rects.backendDx12Button = RECT{ rects.backendAutoButton.right + 6, graphicsRowTop + 3, rects.backendAutoButton.right + 6 + kBackendButtonWidth, graphicsRowTop + kRowHeight - 3 };
    rects.backendVulkanButton = RECT{ rects.backendDx12Button.right + 6, graphicsRowTop + 3, rects.backendDx12Button.right + 6 + kBackendButtonWidth, graphicsRowTop + kRowHeight - 3 };

    const int lightingPathRowTop = RowTopAfter(rects.backendLabel);
    rects.lightingPathLabel = RECT{ left, lightingPathRowTop, left + kLabelWidth, lightingPathRowTop + kRowHeight };
    rects.lightingPathForwardButton = RECT{ left + kLabelWidth, lightingPathRowTop + 3, left + kLabelWidth + kBackendButtonWidth, lightingPathRowTop + kRowHeight - 3 };
    rects.lightingPathForwardPlusButton = RECT{ rects.lightingPathForwardButton.right + 6, lightingPathRowTop + 3, rects.lightingPathForwardButton.right + 6 + kBackendButtonWidth, lightingPathRowTop + kRowHeight - 3 };
    rects.lightingPathDeferredButton = RECT{ rects.lightingPathForwardPlusButton.right + 6, lightingPathRowTop + 3, rects.lightingPathForwardPlusButton.right + 6 + kBackendButtonWidth, lightingPathRowTop + kRowHeight - 3 };

    const int postRowTop = RowTopAfter(rects.lightingPathLabel);
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
    rects.msaa8xButton = RECT{ rects.msaa4xButton.right + 6, msaaRowTop + 3, rects.msaa4xButton.right + 6 + kMsaaButtonWidth, msaaRowTop + kRowHeight - 3 };
    rects.msaa16xButton = RECT{ rects.msaa8xButton.right + 6, msaaRowTop + 3, rects.msaa8xButton.right + 6 + kMsaaButtonWidth, msaaRowTop + kRowHeight - 3 };

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

    // The five level buttons share what the label leaves of the row, up to a comfortable width each.
    const int levelsWidth = std::max(0, right - left - kQualityLabelWidth);
    const int buttonWidth = std::clamp((levelsWidth - 4 * kQualityButtonGap) / 5, 24, kQualityButtonWidth);
    for (int row = 0; row < 4; ++row) {
        const int rowTop = sectionTop + kSectionHeaderHeight + kRowGap + row * (kRowHeight + kDescriptionHeight + kRowGap);
        rects.qualityLabel[row] = RECT{ left, rowTop, left + kQualityLabelWidth, rowTop + kRowHeight };
        for (int level = 0; level < 5; ++level) {
            const int buttonLeft = left + kQualityLabelWidth + level * (buttonWidth + kQualityButtonGap);
            rects.qualityButton[row][level] = RECT{ buttonLeft, rowTop + 3, buttonLeft + buttonWidth, rowTop + kRowHeight - 3 };
        }
    }

    // Pages made of rows: the n-th row under the section header.
    const auto rowTopAt = [&](int row) noexcept {
        return sectionTop + kSectionHeaderHeight + kRowGap + row * (kRowHeight + kDescriptionHeight + kRowGap);
    };
    // A run of equally wide buttons that starts where a row's label ends.
    const auto buttonRow = [&](RECT* buttons, int count, int rowTop, int buttonWidth) noexcept {
        int buttonLeft = left + kLabelWidth;
        for (int index = 0; index < count; ++index) {
            buttons[index] = RECT{ buttonLeft, rowTop + 3, buttonLeft + buttonWidth, rowTop + kRowHeight - 3 };
            buttonLeft += buttonWidth + kButtonGap;
        }
    };
    for (int row = 0; row < 6; ++row) {
        rects.displayLabel[row] = RECT{ left, rowTopAt(row), left + kLabelWidth, rowTopAt(row) + kRowHeight };
    }
    buttonRow(rects.windowModeButton, 2, rowTopAt(0), kModeButtonWidth);
    buttonRow(rects.windowSizeButton, 4, rowTopAt(1), kSizeButtonWidth);
    rects.resizableCheckbox = CheckboxForRow(left, rowTopAt(2));
    rects.vsyncCheckbox = CheckboxForRow(left, rowTopAt(3));
    // The Off button holds a word that is long in some languages; the rates are numbers.
    buttonRow(rects.frameRateButton, 1, rowTopAt(4), kSizeButtonWidth + 8);
    for (int index = 1; index < 5; ++index) {
        const int buttonLeft = rects.frameRateButton[index - 1].right + kButtonGap;
        rects.frameRateButton[index] = RECT{ buttonLeft, rowTopAt(4) + 3, buttonLeft + kRateButtonWidth, rowTopAt(4) + kRowHeight - 3 };
    }
    const auto numberCell = [&](const RECT& previous, int rowTop) noexcept {
        const int cellLeft = previous.right + 2 * kButtonGap;
        return RECT{ cellLeft, rowTop + 3, cellLeft + kNumberCellWidth, rowTop + kRowHeight - 3 };
    };
    rects.windowWidthValue = numberCell(rects.windowSizeButton[3], rowTopAt(1));
    rects.windowHeightValue = RECT{ rects.windowWidthValue.right + kButtonGap, rects.windowWidthValue.top,
        rects.windowWidthValue.right + kButtonGap + kNumberCellWidth, rects.windowWidthValue.bottom };
    rects.frameRateValue = numberCell(rects.frameRateButton[4], rowTopAt(4));
    rects.renderScaleValue = RECT{ left + kLabelWidth, rowTopAt(5) + 3, left + kLabelWidth + kNumberCellWidth, rowTopAt(5) + kRowHeight - 3 };

    // Scenes page: the explanation, the add button, then the list down to the bottom of the pane.
    rects.scenesIntro = RECT{ left, rects.sectionHeader.bottom + kRowGap, right, rects.sectionHeader.bottom + kRowGap + kDescriptionHeight };
    rects.sceneAddButton = RECT{ left, rects.scenesIntro.bottom + kRowGap, left + kSceneAddButtonWidth,
        rects.scenesIntro.bottom + kRowGap + kOptionHeight };
    rects.sceneList = RECT{ left, rects.sceneAddButton.bottom + kRowGap * 2, right, std::max(static_cast<int>(rects.sceneAddButton.bottom), static_cast<int>(content.bottom) - kPadding) };

    // Tags page: where the Scenes page has its add button, the cell for a new name and the button that adds it.
    rects.tagNameField = RECT{ left, rects.scenesIntro.bottom + kRowGap, left + kTagNameFieldWidth,
        rects.scenesIntro.bottom + kRowGap + kOptionHeight };
    rects.tagAddButton = RECT{ rects.tagNameField.right + kButtonGap, rects.tagNameField.top,
        rects.tagNameField.right + kButtonGap + kSceneAddButtonWidth, rects.tagNameField.bottom };

    // Physics page rows 1..4 are the steppers, 5 the gravity presets, 6 the time scale; the Audio page's master
    // volume is the sixth stepper, in the first row.
    constexpr std::array<int, 6> kStepperRows{ 1, 2, 3, 4, 6, 0 };
    for (int stepper = 0; stepper < 6; ++stepper) {
        const int rowTop = rowTopAt(kStepperRows[static_cast<std::size_t>(stepper)]);
        rects.stepperLabel[stepper] = RECT{ left, rowTop, left + kLabelWidth, rowTop + kRowHeight };
        buttonRow(rects.stepperButton[stepper], 1, rowTop, kStepperButtonWidth);
        const RECT& minus = rects.stepperButton[stepper][0];
        rects.stepperValue[stepper] = RECT{ minus.right + kButtonGap, minus.top, minus.right + kButtonGap + kStepperValueWidth, minus.bottom };
        rects.stepperButton[stepper][1] = RECT{ rects.stepperValue[stepper].right + kButtonGap, minus.top,
            rects.stepperValue[stepper].right + kButtonGap + kStepperButtonWidth, minus.bottom };
    }
    rects.gravityPresetLabel = RECT{ left, rowTopAt(5), left + kLabelWidth, rowTopAt(5) + kRowHeight };
    buttonRow(rects.gravityPresetButton, 3, rowTopAt(5), kGravityPresetButtonWidth);
    return rects;
}

RECT ProjectSettingsPanelLayout::CategoryRow(const RECT& sidebar, int index) noexcept {
    const int top = sidebar.top + kCategoryTopInset + CaptionsAbove(index) * kGroupCaptionHeight + (index * kCategoryRowHeight);
    return RECT{ sidebar.left + 6, top, sidebar.right - 6, top + kCategoryRowHeight };
}

RECT ProjectSettingsPanelLayout::GroupCaption(const RECT& sidebar, int group) noexcept {
    const int first = group == 0 ? 0 : (group == 1 ? kEditorGroupStart : kPluginsGroupStart);
    const int bottom = CategoryRow(sidebar, first).top;
    return RECT{ sidebar.left + 12, bottom - kGroupCaptionHeight, sidebar.right - 8, bottom };
}

RECT ProjectSettingsPanelLayout::DescriptionRect(const ProjectSettingsPanelLayoutRects& rects, const RECT& label) noexcept {
    return RECT{ label.left, label.bottom - 6, rects.content.right - kPadding, label.bottom - 6 + kDescriptionHeight };
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
        return rects.lightingPathForwardPlusButton;
    case 2:
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

RECT ProjectSettingsPanelLayout::QualityOptionButton(const ProjectSettingsPanelLayoutRects& rects, int row, int level) noexcept {
    if (row < 0 || row >= 4 || level < 0 || level >= 5) {
        return {};
    }
    return rects.qualityButton[row][level];
}

int ProjectSettingsPanelLayout::SceneRowCapacity(const ProjectSettingsPanelLayoutRects& rects) noexcept {
    return std::max(0, static_cast<int>(rects.sceneList.bottom - rects.sceneList.top + 4) / kSceneRowPitch);
}

RECT ProjectSettingsPanelLayout::SceneRow(const ProjectSettingsPanelLayoutRects& rects, int visibleRow) noexcept {
    const int top = rects.sceneList.top + visibleRow * kSceneRowPitch;
    return RECT{ rects.sceneList.left, top, rects.sceneList.right, top + kRowHeight };
}

RECT ProjectSettingsPanelLayout::SceneRowLabel(const ProjectSettingsPanelLayoutRects& rects, int visibleRow) noexcept {
    RECT row = SceneRow(rects, visibleRow);
    row.right = SceneMoveUpButton(rects, visibleRow).left - kButtonGap;
    return row;
}

RECT ProjectSettingsPanelLayout::SceneMoveUpButton(const ProjectSettingsPanelLayoutRects& rects, int visibleRow) noexcept {
    const RECT row = SceneRow(rects, visibleRow);
    const int right = row.right - kSceneRemoveButtonWidth - kButtonGap;
    return RECT{ right - kSceneUpButtonWidth, row.top + 3, right, row.bottom - 3 };
}

RECT ProjectSettingsPanelLayout::SceneRemoveButton(const ProjectSettingsPanelLayoutRects& rects, int visibleRow) noexcept {
    const RECT row = SceneRow(rects, visibleRow);
    return RECT{ row.right - kSceneRemoveButtonWidth, row.top + 3, row.right, row.bottom - 3 };
}

RECT ProjectSettingsPanelLayout::TagRowLabel(const ProjectSettingsPanelLayoutRects& rects, int visibleRow) noexcept {
    RECT row = SceneRow(rects, visibleRow);
    row.right = SceneRemoveButton(rects, visibleRow).left - kButtonGap;
    return row;
}

RECT ProjectSettingsPanelLayout::MsaaOptionButton(const ProjectSettingsPanelLayoutRects& rects, int index) noexcept {
    switch (index) {
    case 0:
        return rects.msaaOffButton;
    case 1:
        return rects.msaa2xButton;
    case 2:
        return rects.msaa4xButton;
    case 3:
        return rects.msaa8xButton;
    case 4:
        return rects.msaa16xButton;
    default:
        return {};
    }
}

} // namespace kb::editor

#endif
