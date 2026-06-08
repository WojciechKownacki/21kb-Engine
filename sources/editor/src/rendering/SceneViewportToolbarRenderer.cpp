#include "rendering/SceneViewportToolbarRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"

#include <cstdio>
#include <string_view>

namespace kb::editor {
namespace {

constexpr int kToolbarPaddingX = 8;
constexpr int kButtonGap = 6;
constexpr int kButtonHeight = 24;
constexpr int kButtonRadius = 7;

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

void DrawButton(HDC dc, RECT rect, std::string_view text, const EditorTheme& theme, bool active = false) {
    const COLORREF toolbarButton = GdiDrawing::ToColorRef(theme.toolbarButton);
    const COLORREF border = GdiDrawing::ToColorRef(theme.borderPanel);
    const COLORREF fill = active ? Blend(toolbarButton, RGB(64, 142, 168), 5, 10) : toolbarButton;
    const COLORREF stroke = active ? RGB(92, 175, 204) : border;
    const COLORREF textColor = active ? RGB(235, 250, 255) : GdiDrawing::ToColorRef(theme.textPrimary);
    FillRound(dc, rect, fill, stroke, kButtonRadius);
    GdiDrawing::DrawCenteredText(dc, rect, text.data(), textColor);
}

} // namespace

SceneViewportToolbarRects SceneViewportToolbarRenderer::Resolve(const RECT& content) noexcept {
    SceneViewportToolbarRects rects{};
    rects.toolbar = content;
    rects.toolbar.bottom = rects.toolbar.top + Height;
    int cursor = rects.toolbar.left + kToolbarPaddingX;
    rects.profileButton = ButtonRect(rects.toolbar, cursor, 104);
    rects.gridToggleButton = ButtonRect(rects.toolbar, cursor, 74);
    rects.gridStepButton = ButtonRect(rects.toolbar, cursor, 86);
    rects.snapToggleButton = ButtonRect(rects.toolbar, cursor, 76);
    rects.snapStepButton = ButtonRect(rects.toolbar, cursor, 86);
    rects.renderArea = content;
    rects.renderArea.top = rects.toolbar.bottom;
    return rects;
}

void SceneViewportToolbarRenderer::Paint(HDC dc, const RECT& content, const EditorTheme& theme, const EditorViewportPreviewState& state) {
    const SceneViewportToolbarRects rects = Resolve(content);
    GdiDrawing::FillRectColor(dc, rects.toolbar, GdiDrawing::ToColorRef(theme.toolbar));

    const EditorViewportProfile profile = state.Profile();
    DrawButton(dc, rects.profileButton, profile.label.data(), theme);
    DrawButton(dc, rects.gridToggleButton, state.GridVisible() ? "Grid On" : "Grid Off", theme, state.GridVisible());
    char gridStep[32]{};
    std::snprintf(gridStep, sizeof(gridStep), "Grid %s", EditorViewportGridSpacingLabel(state.GridSpacing()));
    DrawButton(dc, rects.gridStepButton, gridStep, theme);
    DrawButton(dc, rects.snapToggleButton, state.SnapEnabled() ? "Snap On" : "Snap Off", theme, state.SnapEnabled());
    char snapStep[32]{};
    std::snprintf(snapStep, sizeof(snapStep), "Snap %s", EditorViewportSnapStepLabel(state.SnapStep()));
    DrawButton(dc, rects.snapStepButton, snapStep, theme);

    char resolution[96]{};
    if (profile.width == 0U || profile.height == 0U) {
        std::snprintf(resolution, sizeof(resolution), "Render: panel");
    } else {
        std::snprintf(resolution, sizeof(resolution), "Render: %ux%u", profile.width, profile.height);
    }
    RECT textRect = rects.toolbar;
    textRect.left = rects.snapStepButton.right + 12;
    textRect.right -= 8;
    GdiDrawing::DrawTabText(dc, textRect, resolution, GdiDrawing::ToColorRef(theme.textDisabled));
}

} // namespace kb::editor

#endif
