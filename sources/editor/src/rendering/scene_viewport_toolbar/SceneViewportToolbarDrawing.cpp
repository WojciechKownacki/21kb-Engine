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
    const COLORREF fill = GdiDrawing::ToColorRef(active ? theme.accent : theme.toolbarButton);
    SceneViewportToolbarDrawing::FillRound(dc, rect, fill, fill, SceneViewportToolbarMetrics::ButtonRadius);
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
    return GdiDrawing::ToColorRef(theme.toolbar);
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

void SceneViewportToolbarDrawing::DrawToolButton(
    HDC dc, RECT rect, HeroIconKind icon, std::string_view key, const EditorTheme& theme, bool active) {
    DrawBase(dc, rect, theme, active);
    const int iconTop = rect.top + ((rect.bottom - rect.top - SceneViewportToolbarMetrics::IconSize) / 2);
    const RECT iconRect{ rect.left + 7, iconTop, rect.left + 7 + SceneViewportToolbarMetrics::IconSize, iconTop + SceneViewportToolbarMetrics::IconSize };
    HeroIconPainter::Draw(dc, iconRect, icon, active ? RGB(235, 250, 255) : GdiDrawing::ToColorRef(theme.textSecondary), 1);
    RECT keyRect{ iconRect.right + 2, rect.top, rect.right - 4, rect.bottom };
    GdiDrawing::DrawTabText(dc, keyRect, key,
        active ? RGB(200, 228, 240) : GdiDrawing::ToColorRef(theme.textDisabled));
}

void SceneViewportToolbarDrawing::DrawValueButton(HDC dc, RECT rect, HeroIconKind icon, std::string_view value, const EditorTheme& theme, bool active, bool dimmed) {
    DrawBase(dc, rect, theme, active);
    const RECT iconRect{
        rect.left + 7,
        rect.top + ((rect.bottom - rect.top - 12) / 2),
        rect.left + 19,
        rect.top + ((rect.bottom - rect.top - 12) / 2) + 12,
    };
    const COLORREF iconColor = active ? RGB(220, 246, 255) : GdiDrawing::ToColorRef(theme.textDisabled);
    HeroIconPainter::Draw(dc, iconRect, icon, iconColor, 1);

    // A dimmed value is one that is no longer current, such as a frame rate held while nothing draws.
    const COLORREF textColor = active ? RGB(235, 250, 255) : GdiDrawing::ToColorRef(dimmed ? theme.textDisabled : theme.textPrimary);
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

void SceneViewportToolbarDrawing::DrawChip(HDC dc, const RECT& rect, std::string_view label, const EditorTheme& theme, bool active) {
    const COLORREF fill = GdiDrawing::ToColorRef(active ? theme.accent : theme.toolbarButton);
    FillRound(dc, rect, fill, fill, SceneViewportToolbarMetrics::ButtonRadius);
    GdiDrawing::DrawCenteredText(dc, rect, label, active ? RGB(239, 252, 255) : GdiDrawing::ToColorRef(theme.textSecondary));
}

void SceneViewportToolbarDrawing::DrawValueBox(HDC dc, const RECT& rect, std::string_view text, const EditorTheme& theme) {
    FillRound(dc, rect, GdiDrawing::ToColorRef(theme.chrome), GdiDrawing::ToColorRef(theme.borderPanel), SceneViewportToolbarMetrics::ButtonRadius);
    GdiDrawing::DrawCenteredText(dc, rect, text, GdiDrawing::ToColorRef(theme.textPrimary));
}

void SceneViewportToolbarDrawing::DrawStepper(
    HDC dc, const RECT& minus, const RECT& value, const RECT& plus, std::string_view text, const EditorTheme& theme) {
    DrawIconButton(dc, minus, HeroIconKind::Minus, theme, false);
    DrawValueBox(dc, value, text, theme);
    DrawIconButton(dc, plus, HeroIconKind::Plus, theme, false);
}

void SceneViewportToolbarDrawing::DrawDivider(HDC dc, const RECT& toolbar, int x, const EditorTheme& theme) {
    const int centerY = toolbar.top + ((toolbar.bottom - toolbar.top) / 2);
    StrokeLine(dc, x, centerY - 10, centerY + 10, GdiDrawing::ToColorRef(theme.borderPanel));
}

} // namespace kb::editor

#endif
