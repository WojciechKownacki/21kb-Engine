#include "rendering/SceneViewportToolbarRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconKind.hpp"
#include "rendering/HeroIconPainter.hpp"

#include <algorithm>
#include <string_view>

namespace kb::editor {
namespace {

constexpr int kToolbarPaddingX = 8;
constexpr int kButtonGap = 4;
constexpr int kGroupGap = 10;
constexpr int kButtonHeight = 24;
constexpr int kIconButtonWidth = 34;
constexpr int kValueButtonWidth = 70;
constexpr int kButtonRadius = 6;
constexpr int kIconSize = 15;
constexpr int kChevronSize = 10;
constexpr int kDropdownTopGap = 4;
constexpr int kDropdownItemWidth = 48;
constexpr int kDropdownItemHeight = 24;
constexpr int kDropdownPadding = 5;

[[nodiscard]] COLORREF Blend(COLORREF a, COLORREF b, int numerator, int denominator) noexcept {
    const int inv = denominator - numerator;
    return RGB(
        (GetRValue(a) * inv + GetRValue(b) * numerator) / denominator,
        (GetGValue(a) * inv + GetGValue(b) * numerator) / denominator,
        (GetBValue(a) * inv + GetBValue(b) * numerator) / denominator);
}

[[nodiscard]] RECT ButtonRect(const RECT& toolbar, int& cursor, int width) noexcept {
    const int toolbarHeight = static_cast<int>(toolbar.bottom - toolbar.top);
    const int top = toolbar.top + ((toolbarHeight - kButtonHeight) / 2);
    RECT rect{
        .left = cursor,
        .top = top,
        .right = cursor + width,
        .bottom = top + kButtonHeight,
    };
    cursor = rect.right + kButtonGap;
    return rect;
}

void AddGroupGap(int& cursor) noexcept {
    cursor += kGroupGap - kButtonGap;
}

[[nodiscard]] RECT CenteredRect(const RECT& rect, int size) noexcept {
    const int width = static_cast<int>(rect.right - rect.left);
    const int height = static_cast<int>(rect.bottom - rect.top);
    const int left = rect.left + ((width - size) / 2);
    const int top = rect.top + ((height - size) / 2);
    return RECT{ left, top, left + size, top + size };
}

void FillRound(HDC dc, const RECT& rect, COLORREF fill, COLORREF border, int radius) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void StrokeLine(HDC dc, int x, int top, int bottom, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    MoveToEx(dc, x, top, nullptr);
    LineTo(dc, x, bottom);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void DrawBase(HDC dc, RECT rect, const EditorTheme& theme, bool active) {
    const COLORREF toolbarButton = GdiDrawing::ToColorRef(theme.toolbarButton);
    const COLORREF border = GdiDrawing::ToColorRef(theme.borderPanel);
    const COLORREF fill = active ? Blend(toolbarButton, RGB(55, 119, 142), 5, 10) : Blend(toolbarButton, RGB(0, 0, 0), 1, 8);
    const COLORREF stroke = active ? RGB(86, 172, 205) : Blend(border, RGB(96, 109, 132), 1, 5);
    FillRound(dc, rect, fill, stroke, kButtonRadius);
    const RECT highlight{ rect.left + 2, rect.top + 1, rect.right - 2, rect.top + 2 };
    GdiDrawing::FillRectColor(dc, highlight, active ? RGB(85, 150, 174) : RGB(47, 53, 64));
}

void DrawIconButton(HDC dc, RECT rect, HeroIconKind icon, const EditorTheme& theme, bool active) {
    DrawBase(dc, rect, theme, active);
    const COLORREF iconColor = active ? RGB(235, 250, 255) : GdiDrawing::ToColorRef(theme.textSecondary);
    HeroIconPainter::Draw(dc, CenteredRect(rect, kIconSize), icon, iconColor, 1);
}

void DrawValueButton(HDC dc, RECT rect, HeroIconKind icon, const char* value, const EditorTheme& theme) {
    DrawBase(dc, rect, theme, false);
    const RECT iconRect{
        rect.left + 7,
        rect.top + ((rect.bottom - rect.top - 12) / 2),
        rect.left + 19,
        rect.top + ((rect.bottom - rect.top - 12) / 2) + 12,
    };
    HeroIconPainter::Draw(dc, iconRect, icon, GdiDrawing::ToColorRef(theme.textDisabled), 1);
    const COLORREF textColor = GdiDrawing::ToColorRef(theme.textPrimary);
    RECT textRect = rect;
    textRect.left += 23;
    textRect.right -= 18;
    GdiDrawing::DrawTabText(dc, textRect, value, textColor);
    const RECT chevronRect{
        rect.right - 15,
        rect.top + ((rect.bottom - rect.top - kChevronSize) / 2),
        rect.right - 15 + kChevronSize,
        rect.top + ((rect.bottom - rect.top - kChevronSize) / 2) + kChevronSize,
    };
    HeroIconPainter::Draw(dc, chevronRect, HeroIconKind::ChevronDown, GdiDrawing::ToColorRef(theme.textDisabled), 1);
}

void DrawDivider(HDC dc, const RECT& toolbar, int x, const EditorTheme& theme) {
    const int centerY = toolbar.top + ((toolbar.bottom - toolbar.top) / 2);
    StrokeLine(dc, x, centerY - 10, centerY + 10, GdiDrawing::ToColorRef(theme.borderPanel));
}

[[nodiscard]] COLORREF ToolbarRowColor(const EditorTheme& theme) noexcept {
    return Blend(GdiDrawing::ToColorRef(theme.toolbar), RGB(0, 0, 0), 1, 6);
}

} // namespace

SceneViewportToolbarRects SceneViewportToolbarRenderer::Resolve(const RECT& content) noexcept {
    EditorViewportPreviewState closedState;
    return Resolve(content, closedState);
}

SceneViewportToolbarRects SceneViewportToolbarRenderer::Resolve(const RECT& content, const EditorViewportPreviewState& state) noexcept {
    SceneViewportToolbarRects rects{};
    rects.toolbar = content;
    rects.toolbar.bottom = rects.toolbar.top + Height;
    rects.row = rects.toolbar;
    int cursor = rects.toolbar.left + kToolbarPaddingX;
    rects.gridToggleButton = ButtonRect(rects.row, cursor, kIconButtonWidth);
    rects.gridStepButton = ButtonRect(rects.row, cursor, kValueButtonWidth);
    AddGroupGap(cursor);
    rects.snapToggleButton = ButtonRect(rects.row, cursor, kIconButtonWidth);
    rects.snapStepButton = ButtonRect(rects.row, cursor, kValueButtonWidth);
    if (state.ToolbarDropdown() != EditorViewportToolbarDropdown::None) {
        const RECT anchor = state.ToolbarDropdown() == EditorViewportToolbarDropdown::GridSpacing ? rects.gridStepButton : rects.snapStepButton;
        const int optionCount = 6;
        const int dropdownWidth = (optionCount * kDropdownItemWidth) + (kDropdownPadding * 2);
        const int left = std::max(content.left + kToolbarPaddingX, std::min(anchor.left, content.right - dropdownWidth - kToolbarPaddingX));
        rects.dropdownPanel = RECT{
            left,
            rects.row.bottom + kDropdownTopGap,
            left + dropdownWidth,
            rects.row.bottom + kDropdownTopGap + kDropdownItemHeight + (kDropdownPadding * 2),
        };
        for (int index = 0; index < optionCount; ++index) {
            rects.dropdownItems[static_cast<std::size_t>(index)] = RECT{
                rects.dropdownPanel.left + kDropdownPadding + (index * kDropdownItemWidth),
                rects.dropdownPanel.top + kDropdownPadding,
                rects.dropdownPanel.left + kDropdownPadding + ((index + 1) * kDropdownItemWidth),
                rects.dropdownPanel.top + kDropdownPadding + kDropdownItemHeight,
            };
        }
    }
    rects.renderArea = content;
    rects.renderArea.top = rects.toolbar.bottom;
    return rects;
}

void SceneViewportToolbarRenderer::Paint(HDC dc, const RECT& content, const EditorTheme& theme, const EditorViewportPreviewState& state) {
    const SceneViewportToolbarRects rects = Resolve(content, state);
    GdiDrawing::FillRectColor(dc, rects.row, ToolbarRowColor(theme));

    DrawIconButton(dc, rects.gridToggleButton, HeroIconKind::Eye, theme, state.GridVisible());
    DrawValueButton(dc, rects.gridStepButton, HeroIconKind::AdjustmentsHorizontal, EditorViewportGridSpacingLabel(state.GridSpacing()), theme);
    DrawDivider(dc, rects.toolbar, rects.gridStepButton.right + 7, theme);
    DrawIconButton(dc, rects.snapToggleButton, HeroIconKind::Cube, theme, state.SnapEnabled());
    DrawValueButton(dc, rects.snapStepButton, HeroIconKind::AdjustmentsHorizontal, EditorViewportSnapStepLabel(state.SnapStep()), theme);
}

} // namespace kb::editor

#endif
