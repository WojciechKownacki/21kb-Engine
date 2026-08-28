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
    return Resolve(content, state, false);
}

SceneViewportToolbarRects SceneViewportToolbarLayout::Resolve(
    const RECT& content,
    const EditorViewportPreviewState& state,
    bool terrainToolsVisible) noexcept {
    SceneViewportToolbarRects rects{};
    rects.toolbar = content;
    rects.toolbar.bottom = rects.toolbar.top + SceneViewportToolbarRenderer::Height;
    rects.row = rects.toolbar;

    int cursor = rects.toolbar.left + SceneViewportToolbarMetrics::PaddingX;
    rects.fpsCounter = ButtonRect(rects.row, cursor, SceneViewportToolbarMetrics::FpsCounterWidth);
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
    if (terrainToolsVisible) {
        const TerrainViewportToolbarRects terrainTools = ResolveTerrainTools(content);
        rects.renderArea.top = std::min<LONG>(
            rects.renderArea.bottom,
            terrainTools.panel.bottom + 8);
    }
    return rects;
}

TerrainViewportToolbarRects SceneViewportToolbarLayout::ResolveTerrainTools(const RECT& content) noexcept {
    TerrainViewportToolbarRects rects{};
    constexpr int panelHeight = 38;
    constexpr int buttonHeight = 26;
    constexpr int gap = 3;
    int cursor = content.left + 8;
    const int top = content.top + SceneViewportToolbarRenderer::Height + 8;
    const int panelLeft = cursor;
    cursor += 7;
    const int buttonTop = top + (panelHeight - buttonHeight) / 2;
    const auto button = [&](int width) {
        const RECT value{ cursor, buttonTop, cursor + width, buttonTop + buttonHeight };
        cursor = value.right + gap;
        return value;
    };
    rects.selectButton = button(48);
    rects.sculptButton = button(50);
    rects.holesButton = button(46);
    rects.paintButton = button(46);
    cursor += 1;
    rects.brushButton = button(95);
    rects.brushShapeButton = button(110);
    cursor += 1;
    rects.sizeMinusButton = button(24);
    rects.sizeValue = button(58);
    rects.sizePlusButton = button(24);
    cursor += 1;
    rects.strengthMinusButton = button(24);
    rects.strengthValue = button(72);
    rects.strengthPlusButton = button(24);
    rects.panel = RECT{ panelLeft, top, rects.strengthPlusButton.right + 7, top + panelHeight };

    constexpr int operationMenuWidth = 356;
    constexpr int operationHeaderHeight = 34;
    constexpr int operationItemHeight = 55;
    const int operationLeft = std::clamp(
        static_cast<int>(rects.brushButton.left),
        static_cast<int>(content.left + 8),
        std::max(static_cast<int>(content.left + 8), static_cast<int>(content.right - operationMenuWidth - 8)));
    rects.brushMenu = RECT{
        operationLeft,
        rects.panel.bottom + 5,
        operationLeft + operationMenuWidth,
        rects.panel.bottom + 5 + operationHeaderHeight + operationItemHeight * 4 + 8,
    };
    for (std::size_t index = 0U; index < rects.brushItems.size(); ++index) {
        const int column = static_cast<int>(index % 2U);
        const int row = static_cast<int>(index / 2U);
        rects.brushItems[index] = RECT{
            rects.brushMenu.left + 6 + column * 172,
            rects.brushMenu.top + operationHeaderHeight + row * operationItemHeight,
            rects.brushMenu.left + 6 + column * 172 + 168,
            rects.brushMenu.top + operationHeaderHeight + row * operationItemHeight + 51,
        };
    }

    constexpr int shapeMenuWidth = 404;
    constexpr int shapeHeaderHeight = 38;
    constexpr int shapeItemHeight = 72;
    const int shapeLeft = std::clamp(
        static_cast<int>(rects.brushShapeButton.left),
        static_cast<int>(content.left + 8),
        std::max(static_cast<int>(content.left + 8), static_cast<int>(content.right - shapeMenuWidth - 8)));
    rects.brushShapeMenu = RECT{
        shapeLeft,
        rects.panel.bottom + 5,
        shapeLeft + shapeMenuWidth,
        rects.panel.bottom + 5 + shapeHeaderHeight + shapeItemHeight * 3 + 8,
    };
    for (std::size_t index = 0U; index < rects.brushShapeItems.size(); ++index) {
        const int column = static_cast<int>(index % 2U);
        const int row = static_cast<int>(index / 2U);
        rects.brushShapeItems[index] = RECT{
            rects.brushShapeMenu.left + 6 + column * 196,
            rects.brushShapeMenu.top + shapeHeaderHeight + row * shapeItemHeight,
            rects.brushShapeMenu.left + 6 + column * 196 + 192,
            rects.brushShapeMenu.top + shapeHeaderHeight + row * shapeItemHeight + 68,
        };
    }
    return rects;
}

} // namespace kb::editor

#endif
