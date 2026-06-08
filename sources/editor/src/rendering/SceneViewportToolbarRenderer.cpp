#include "rendering/SceneViewportToolbarRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconKind.hpp"
#include "rendering/HeroIconPainter.hpp"

#include <cstdio>
#include <cstring>
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

[[nodiscard]] RECT InsetLeft(RECT rect, int amount) noexcept {
    rect.left += amount;
    return rect;
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

void DrawStatusPill(HDC dc, RECT rect, const char* text, const EditorTheme& theme) {
    rect = InsetLeft(rect, 2);
    GdiDrawing::DrawTabText(dc, rect, text, GdiDrawing::ToColorRef(theme.textDisabled));
}

} // namespace

SceneViewportToolbarRects SceneViewportToolbarRenderer::Resolve(const RECT& content) noexcept {
    SceneViewportToolbarRects rects{};
    rects.toolbar = content;
    rects.toolbar.bottom = rects.toolbar.top + Height;
    int cursor = rects.toolbar.left + kToolbarPaddingX;
    rects.gridToggleButton = ButtonRect(rects.toolbar, cursor, kIconButtonWidth);
    rects.gridStepButton = ButtonRect(rects.toolbar, cursor, kValueButtonWidth);
    AddGroupGap(cursor);
    rects.snapToggleButton = ButtonRect(rects.toolbar, cursor, kIconButtonWidth);
    rects.snapStepButton = ButtonRect(rects.toolbar, cursor, kValueButtonWidth);
    rects.renderArea = content;
    rects.renderArea.top = rects.toolbar.bottom;
    return rects;
}

void SceneViewportToolbarRenderer::Paint(HDC dc, const RECT& content, const EditorTheme& theme, const EditorViewportPreviewState& state) {
    const SceneViewportToolbarRects rects = Resolve(content);
    GdiDrawing::FillRectColor(dc, rects.toolbar, GdiDrawing::ToColorRef(theme.toolbar));

    DrawIconButton(dc, rects.gridToggleButton, HeroIconKind::Eye, theme, state.GridVisible());
    DrawValueButton(dc, rects.gridStepButton, HeroIconKind::AdjustmentsHorizontal, EditorViewportGridSpacingLabel(state.GridSpacing()), theme);
    DrawDivider(dc, rects.toolbar, rects.gridStepButton.right + 7, theme);
    DrawIconButton(dc, rects.snapToggleButton, HeroIconKind::Cube, theme, state.SnapEnabled());
    DrawValueButton(dc, rects.snapStepButton, HeroIconKind::AdjustmentsHorizontal, EditorViewportSnapStepLabel(state.SnapStep()), theme);

    const EditorViewportProfile profile = state.Profile();
    char resolution[96]{};
    if (profile.width == 0U || profile.height == 0U) {
        std::snprintf(resolution, sizeof(resolution), "Render: panel");
    } else {
        std::snprintf(resolution, sizeof(resolution), "Render: %ux%u", profile.width, profile.height);
    }
    RECT textRect = rects.toolbar;
    textRect.left = rects.snapStepButton.right + 14;
    textRect.right -= 8;
    DrawStatusPill(dc, textRect, resolution, theme);
}

} // namespace kb::editor

#endif
