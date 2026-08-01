#include "rendering/scene_viewport_toolbar/SceneViewportToolbarLayout.hpp"

#if defined(_WIN32)
#include "rendering/scene_viewport_toolbar/SceneViewportToolbarMetrics.hpp"

#include <algorithm>

namespace kb::editor {
namespace {

[[nodiscard]] RECT ButtonRect(const RECT& toolbar, int& cursor, int width) noexcept {
    const int toolbarHeight = static_cast<int>(toolbar.bottom - toolbar.top);
    const int top = toolbar.top + ((toolbarHeight - SceneViewportToolbarMetrics::ButtonHeight) / 2);
    RECT rect{
        .left = cursor,
        .top = top,
        .right = cursor + width,
        .bottom = top + SceneViewportToolbarMetrics::ButtonHeight,
    };
    cursor = rect.right + SceneViewportToolbarMetrics::ButtonGap;
    return rect;
}

void AddGroupGap(int& cursor) noexcept {
    cursor += SceneViewportToolbarMetrics::GroupGap - SceneViewportToolbarMetrics::ButtonGap;
}

[[nodiscard]] int DropdownOptionCount(EditorViewportToolbarDropdown dropdown) noexcept {
    switch (dropdown) {
    case EditorViewportToolbarDropdown::GridSpacing:
        return static_cast<int>(EditorViewportGridSpacingOptionCount());
    case EditorViewportToolbarDropdown::RotationSnap:
        return static_cast<int>(EditorViewportRotationSnapOptionCount());
    case EditorViewportToolbarDropdown::SnapStep:
        return static_cast<int>(EditorViewportSnapStepOptionCount());
    case EditorViewportToolbarDropdown::None:
    default:
        return 0;
    }
}

[[nodiscard]] RECT DropdownAnchor(const SceneViewportToolbarRects& rects, EditorViewportToolbarDropdown dropdown) noexcept {
    switch (dropdown) {
    case EditorViewportToolbarDropdown::GridSpacing:
        return rects.gridStepButton;
    case EditorViewportToolbarDropdown::RotationSnap:
        return rects.rotationSnapButton;
    case EditorViewportToolbarDropdown::SnapStep:
    case EditorViewportToolbarDropdown::None:
    default:
        return rects.snapStepButton;
    }
}

} // namespace

SceneViewportToolbarRects SceneViewportToolbarLayout::Resolve(const RECT& content) noexcept {
    EditorViewportPreviewState closedState;
    return Resolve(content, closedState);
}

SceneViewportToolbarRects SceneViewportToolbarLayout::Resolve(const RECT& content, const EditorViewportPreviewState& state) noexcept {
    SceneViewportToolbarRects rects{};
    rects.toolbar = content;
    rects.toolbar.bottom = rects.toolbar.top + SceneViewportToolbarRenderer::Height;
    rects.row = rects.toolbar;

    int cursor = rects.toolbar.left + SceneViewportToolbarMetrics::PaddingX;
    rects.fpsCounter = ButtonRect(rects.row, cursor, SceneViewportToolbarMetrics::FpsCounterWidth);
    rects.renderStats = ButtonRect(rects.row, cursor, SceneViewportToolbarMetrics::RenderStatsWidth);
    rects.ecsStats = ButtonRect(rects.row, cursor, SceneViewportToolbarMetrics::EcsStatsWidth);
    AddGroupGap(cursor);
    rects.renderProfileButton = ButtonRect(rects.row, cursor, SceneViewportToolbarMetrics::ProfileButtonWidth);
    AddGroupGap(cursor);
    rects.gridToggleButton = ButtonRect(rects.row, cursor, SceneViewportToolbarMetrics::IconButtonWidth);
    rects.gridStepButton = ButtonRect(rects.row, cursor, SceneViewportToolbarMetrics::ValueButtonWidth);
    AddGroupGap(cursor);
    rects.snapToggleButton = ButtonRect(rects.row, cursor, SceneViewportToolbarMetrics::IconButtonWidth);
    rects.snapStepButton = ButtonRect(rects.row, cursor, SceneViewportToolbarMetrics::ValueButtonWidth);
    rects.rotationSnapButton = ButtonRect(rects.row, cursor, SceneViewportToolbarMetrics::ValueButtonWidth);

    const EditorViewportToolbarDropdown dropdown = state.ToolbarDropdown();
    const int optionCount = DropdownOptionCount(dropdown);
    if (optionCount > 0) {
        const RECT anchor = DropdownAnchor(rects, dropdown);
        const int dropdownWidth = (optionCount * SceneViewportToolbarMetrics::DropdownItemWidth) + (SceneViewportToolbarMetrics::DropdownPadding * 2);
        const int left = std::max(
            content.left + SceneViewportToolbarMetrics::PaddingX,
            std::min(anchor.left, content.right - dropdownWidth - SceneViewportToolbarMetrics::PaddingX));
        rects.dropdownPanel = RECT{
            left,
            rects.row.bottom + SceneViewportToolbarMetrics::DropdownTopGap,
            left + dropdownWidth,
            rects.row.bottom + SceneViewportToolbarMetrics::DropdownTopGap + SceneViewportToolbarMetrics::DropdownItemHeight + (SceneViewportToolbarMetrics::DropdownPadding * 2),
        };
        for (int index = 0; index < optionCount; ++index) {
            rects.dropdownItems[static_cast<std::size_t>(index)] = RECT{
                rects.dropdownPanel.left + SceneViewportToolbarMetrics::DropdownPadding + (index * SceneViewportToolbarMetrics::DropdownItemWidth),
                rects.dropdownPanel.top + SceneViewportToolbarMetrics::DropdownPadding,
                rects.dropdownPanel.left + SceneViewportToolbarMetrics::DropdownPadding + ((index + 1) * SceneViewportToolbarMetrics::DropdownItemWidth),
                rects.dropdownPanel.top + SceneViewportToolbarMetrics::DropdownPadding + SceneViewportToolbarMetrics::DropdownItemHeight,
            };
        }
    }

    rects.renderArea = content;
    rects.renderArea.top = rects.toolbar.bottom;
    return rects;
}

TerrainViewportToolbarRects SceneViewportToolbarLayout::ResolveTerrainTools(const RECT& content) noexcept {
    TerrainViewportToolbarRects rects{};
    constexpr int panelWidth = 690;
    constexpr int panelHeight = 38;
    constexpr int buttonHeight = 26;
    constexpr int gap = 4;
    int cursor = content.left + 8;
    const int top = content.top + SceneViewportToolbarRenderer::Height + 8;
    rects.panel = RECT{
        cursor,
        top,
        std::min<LONG>(content.right - 8, static_cast<LONG>(cursor + panelWidth)),
        top + panelHeight,
    };
    cursor += 7;
    const int buttonTop = top + (panelHeight - buttonHeight) / 2;
    const auto button = [&](int width) {
        const RECT value{ cursor, buttonTop, cursor + width, buttonTop + buttonHeight };
        cursor = value.right + gap;
        return value;
    };
    rects.selectButton = button(58);
    rects.sculptButton = button(64);
    rects.holesButton = button(58);
    cursor += 5;
    rects.brushButton = button(164);
    cursor += 5;
    rects.sizeMinusButton = button(26);
    rects.sizeValue = button(72);
    rects.sizePlusButton = button(26);
    cursor += 5;
    rects.strengthMinusButton = button(26);
    rects.strengthValue = button(88);
    rects.strengthPlusButton = button(26);

    constexpr int brushItemHeight = 27;
    rects.brushMenu = RECT{
        rects.brushButton.left,
        rects.panel.bottom + 5,
        rects.brushButton.right,
        rects.panel.bottom + 5 + brushItemHeight * static_cast<int>(rects.brushItems.size()) + 8,
    };
    for (std::size_t index = 0U; index < rects.brushItems.size(); ++index) {
        rects.brushItems[index] = RECT{
            rects.brushMenu.left + 4,
            rects.brushMenu.top + 4 + static_cast<int>(index) * brushItemHeight,
            rects.brushMenu.right - 4,
            rects.brushMenu.top + 4 + static_cast<int>(index + 1U) * brushItemHeight,
        };
    }
    return rects;
}

} // namespace kb::editor

#endif
