#include "rendering/scene_viewport_toolbar/SceneViewportToolbarDrawing.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/scene_viewport_toolbar/SceneViewportToolbarMetrics.hpp"

namespace kb::editor {
namespace {

[[nodiscard]] RECT CenteredRect(const RECT& rect, int size) noexcept {
    const int width = static_cast<int>(rect.right - rect.left);
    const int height = static_cast<int>(rect.bottom - rect.top);
    const int left = rect.left + ((width - size) / 2);
    const int top = rect.top + ((height - size) / 2);
    return RECT{ left, top, left + size, top + size };
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
    const COLORREF fill = active
        ? SceneViewportToolbarDrawing::Blend(toolbarButton, RGB(55, 119, 142), 5, 10)
        : SceneViewportToolbarDrawing::Blend(toolbarButton, RGB(0, 0, 0), 1, 8);
    const COLORREF stroke = active ? RGB(86, 172, 205) : SceneViewportToolbarDrawing::Blend(border, RGB(96, 109, 132), 1, 5);
    SceneViewportToolbarDrawing::FillRound(dc, rect, fill, stroke, SceneViewportToolbarMetrics::ButtonRadius);
    const RECT highlight{ rect.left + 2, rect.top + 1, rect.right - 2, rect.top + 2 };
    GdiDrawing::FillRectColor(dc, highlight, active ? RGB(85, 150, 174) : RGB(47, 53, 64));
}

} // namespace

COLORREF SceneViewportToolbarDrawing::Blend(COLORREF a, COLORREF b, int numerator, int denominator) noexcept {
    const int inv = denominator - numerator;
    return RGB(
        (GetRValue(a) * inv + GetRValue(b) * numerator) / denominator,
        (GetGValue(a) * inv + GetGValue(b) * numerator) / denominator,
        (GetBValue(a) * inv + GetBValue(b) * numerator) / denominator);
}

COLORREF SceneViewportToolbarDrawing::ToolbarRowColor(const EditorTheme& theme) noexcept {
    return Blend(GdiDrawing::ToColorRef(theme.toolbar), RGB(0, 0, 0), 1, 6);
}

void SceneViewportToolbarDrawing::FillRound(HDC dc, const RECT& rect, COLORREF fill, COLORREF border, int radius) {
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

void SceneViewportToolbarDrawing::DrawIconButton(HDC dc, RECT rect, HeroIconKind icon, const EditorTheme& theme, bool active) {
    DrawBase(dc, rect, theme, active);
    const COLORREF iconColor = active ? RGB(235, 250, 255) : GdiDrawing::ToColorRef(theme.textSecondary);
    HeroIconPainter::Draw(dc, CenteredRect(rect, SceneViewportToolbarMetrics::IconSize), icon, iconColor, 1);
}

void SceneViewportToolbarDrawing::DrawValueButton(HDC dc, RECT rect, HeroIconKind icon, const char* value, const EditorTheme& theme, bool active) {
    DrawBase(dc, rect, theme, active);
    const RECT iconRect{
        rect.left + 7,
        rect.top + ((rect.bottom - rect.top - 12) / 2),
        rect.left + 19,
        rect.top + ((rect.bottom - rect.top - 12) / 2) + 12,
    };
    const COLORREF iconColor = active ? RGB(220, 246, 255) : GdiDrawing::ToColorRef(theme.textDisabled);
    HeroIconPainter::Draw(dc, iconRect, icon, iconColor, 1);

    const COLORREF textColor = active ? RGB(235, 250, 255) : GdiDrawing::ToColorRef(theme.textPrimary);
    RECT textRect = rect;
    textRect.left += 23;
    textRect.right -= 18;
    GdiDrawing::DrawTabText(dc, textRect, value, textColor);

    const RECT chevronRect{
        rect.right - 15,
        rect.top + ((rect.bottom - rect.top - SceneViewportToolbarMetrics::ChevronSize) / 2),
        rect.right - 15 + SceneViewportToolbarMetrics::ChevronSize,
        rect.top + ((rect.bottom - rect.top - SceneViewportToolbarMetrics::ChevronSize) / 2) + SceneViewportToolbarMetrics::ChevronSize,
    };
    HeroIconPainter::Draw(dc, chevronRect, HeroIconKind::ChevronDown, GdiDrawing::ToColorRef(theme.textDisabled), 1);
}

void SceneViewportToolbarDrawing::DrawDivider(HDC dc, const RECT& toolbar, int x, const EditorTheme& theme) {
    const int centerY = toolbar.top + ((toolbar.bottom - toolbar.top) / 2);
    StrokeLine(dc, x, centerY - 10, centerY + 10, GdiDrawing::ToColorRef(theme.borderPanel));
}

} // namespace kb::editor

#endif
