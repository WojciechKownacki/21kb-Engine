#include "rendering/CurveEditorRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"
#include "platform/win32/Utf8Gdi.hpp"
#endif

namespace kb::editor {

float CurveEditorRenderer::GridStep(float span, float targetLines) noexcept {
    if (!(span > 0.0F) || !(targetLines > 0.0F)) return 1.0F;
    const float raw = span / targetLines;
    const float power = std::pow(10.0F, std::floor(std::log10(raw)));
    const float scaled = raw / power;
    const float nice = scaled < 1.5F ? 1.0F : (scaled < 3.5F ? 2.0F : (scaled < 7.5F ? 5.0F : 10.0F));
    return nice * power;
}

#if defined(_WIN32)
namespace {

[[nodiscard]] COLORREF ThemeColor(EditorColor color) noexcept {
    return RGB(color.r, color.g, color.b);
}

void Line(HDC dc, int x0, int y0, int x1, int y1) {
    MoveToEx(dc, x0, y0, nullptr);
    LineTo(dc, x1, y1);
}

void Label(HDC dc, RECT rect, float value, UINT format) {
    char text[32]{};
    std::snprintf(text, sizeof(text), "%g", static_cast<double>(value));
    kb::editor::DrawTextUtf8(dc, text, -1, &rect, format | DT_SINGLELINE | DT_NOPREFIX);
}

void Square(HDC dc, float x, float y, int half, COLORREF color) {
    const int cx = static_cast<int>(std::lround(x));
    const int cy = static_cast<int>(std::lround(y));
    GdiDrawing::FillRectColor(dc, RECT{ cx - half, cy - half, cx + half + 1, cy + half + 1 }, color);
}

} // namespace

void CurveEditorRenderer::Paint(HDC dc, const RECT& rect, const CurveEditorState& state, const EditorTheme& theme) {
    GdiDrawing::FillRectColor(dc, rect, ThemeColor(theme.background));
    if (rect.right - rect.left < 8 || rect.bottom - rect.top < 8) return;
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, rect.left, rect.top, rect.right, rect.bottom);
    const ScopedFont font{ 11, FW_NORMAL };
    const ScopedGdiObject selectedFont(dc, font.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, ThemeColor(theme.textSecondary));

    const COLORREF gridColor = ThemeColor(theme.gridLine);
    const float timeStep = GridStep(state.TimeEnd() - state.TimeStart(), 8.0F);
    for (float time = std::ceil(state.TimeStart() / timeStep) * timeStep; time <= state.TimeEnd(); time += timeStep) {
        const int x = static_cast<int>(std::lround(state.XAt(time)));
        GdiDrawing::FillRectColor(dc, RECT{ x, rect.top, x + 1, rect.bottom }, gridColor);
        Label(dc, RECT{ x + 3, rect.bottom - 16, x + 70, rect.bottom }, std::abs(time) < timeStep * 1.0e-3F ? 0.0F : time, DT_LEFT | DT_BOTTOM);
    }
    const float valueStep = GridStep(state.ValueEnd() - state.ValueStart(), 6.0F);
    for (float value = std::ceil(state.ValueStart() / valueStep) * valueStep; value <= state.ValueEnd(); value += valueStep) {
        const int y = static_cast<int>(std::lround(state.YAt(value)));
        GdiDrawing::FillRectColor(dc, RECT{ rect.left, y, rect.right, y + 1 }, gridColor);
        Label(dc, RECT{ rect.left + 3, y - 14, rect.left + 70, y }, std::abs(value) < valueStep * 1.0e-3F ? 0.0F : value, DT_LEFT | DT_BOTTOM);
    }

    const kb::math::Curve& curve = state.Curve();
    const COLORREF accent = ThemeColor(theme.accent);
    HPEN curvePen = CreatePen(PS_SOLID, 2, RGB(245, 191, 94));
    HGDIOBJ previousPen = SelectObject(dc, curvePen);
    for (LONG x = rect.left; x < rect.right; x += 2) {
        const float y = state.YAt(kb::math::Evaluate(curve, state.TimeAt(static_cast<float>(x))));
        const int py = static_cast<int>(std::lround(std::clamp(y, -1.0e5F, 1.0e5F)));
        if (x == rect.left) MoveToEx(dc, x, py, nullptr);
        else LineTo(dc, x, py);
    }
    SelectObject(dc, previousPen);
    DeleteObject(curvePen);

    const std::optional<std::size_t> selected = state.Selected();
    if (selected.has_value() && CurveEditorState::HasHandles(curve.keyframes[*selected])) {
        HPEN handlePen = CreatePen(PS_SOLID, 1, ThemeColor(theme.textSecondary));
        HGDIOBJ oldPen = SelectObject(dc, handlePen);
        const kb::math::CurveKeyframe& key = curve.keyframes[*selected];
        const int kx = static_cast<int>(std::lround(state.XAt(key.time)));
        const int ky = static_cast<int>(std::lround(state.YAt(key.value)));
        for (const bool outSide : { false, true }) {
            const auto handle = state.HandlePosition(*selected, outSide);
            Line(dc, kx, ky, static_cast<int>(std::lround(handle.first)), static_cast<int>(std::lround(handle.second)));
            Square(dc, handle.first, handle.second, 3, ThemeColor(theme.textPrimary));
        }
        SelectObject(dc, oldPen);
        DeleteObject(handlePen);
    }
    for (std::size_t index = 0U; index < curve.keyframes.size(); ++index) {
        const bool isSelected = selected.has_value() && *selected == index;
        Square(dc, state.XAt(curve.keyframes[index].time), state.YAt(curve.keyframes[index].value), isSelected ? 4 : 3,
            isSelected ? accent : ThemeColor(theme.textPrimary));
    }
    RestoreDC(dc, saved);
}
#endif

} // namespace kb::editor
