#pragma once

// The look every settings-style editor panel shares: the metrics a row is built
// from, the rounded section and input frames, and the text and divider helpers.
// It lives apart from any one panel so a new panel inherits the Inspector's
// appearance by including this, instead of restating a second set of numbers
// that then drifts from the first.

#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/GdiResources.hpp"
#include "rendering/ProjectFilesPanelDrawing.hpp"
#include "rendering/HeroIconGdiplusRuntime.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

#pragma warning(push, 0)
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>
#pragma warning(pop)

#include <algorithm>
#include <string>
#include <string_view>

namespace kb::editor::panel_style {


constexpr int kSectionHeaderHeight = 28;
constexpr int kFieldRowHeight = 26;
constexpr int kValueHeight = 22;
constexpr int kRowPadX = 14;
constexpr int kAssetPickerButtonSize = 18;
constexpr int kAssetPickerButtonGap = 3;
constexpr int kAxisLetterWidth = 11;
constexpr int kAxisGap = 6;
constexpr int kLaneGap = 5;
constexpr int kDividerHeight = 1;
constexpr int kCheckboxSize = 16;
constexpr int kTextBaselineOffsetY = 1;
constexpr int kSectionDividerInset = 8;
constexpr float kSectionCornerRadius = 7.0F;
constexpr float kSectionOutlineWidth = 1.5F;
constexpr float kInputCornerRadius = 3.0F;
constexpr float kInputOutlineWidth = 1.0F;
[[nodiscard]] inline COLORREF Color(EditorColor color) {
    return GdiDrawing::ToColorRef(color);
}

[[nodiscard]] inline COLORREF Rgb(int red, int green, int blue) noexcept {
    return RGB(red, green, blue);
}

[[nodiscard]] inline COLORREF HoverFill(const EditorTheme& theme) noexcept {
    const COLORREF panel = Color(theme.panel);
    const COLORREF accent = Color(theme.accent);
    constexpr int accentPercent = 9;
    constexpr int panelPercent = 100 - accentPercent;
    return RGB(
        (GetRValue(panel) * panelPercent + GetRValue(accent) * accentPercent) / 100,
        (GetGValue(panel) * panelPercent + GetGValue(accent) * accentPercent) / 100,
        (GetBValue(panel) * panelPercent + GetBValue(accent) * accentPercent) / 100);
}

[[nodiscard]] inline RECT Rect(int left, int top, int right, int bottom) noexcept {
    return RECT{ left, top, right, bottom };
}

[[nodiscard]] inline RECT Shrink(RECT rect, int left, int top, int right, int bottom) noexcept {
    rect.left += left;
    rect.top += top;
    rect.right -= right;
    rect.bottom -= bottom;
    return rect;
}

[[nodiscard]] inline int CenteredY(const RECT& outer, int height) noexcept {
    return static_cast<int>(outer.top) + std::max(0, (static_cast<int>(outer.bottom - outer.top) - height) / 2);
}

[[nodiscard]] inline RECT CenteredRect(const RECT& outer, int left, int width, int height) noexcept {
    const int top = CenteredY(outer, height);
    return Rect(left, top, left + width, top + height);
}


inline void Text(HDC dc, RECT rect, std::string_view text, COLORREF color, UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    if (text.empty()) return;
    rect.top += kTextBaselineOffsetY;
    rect.bottom += kTextBaselineOffsetY;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    const int wideLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (wideLength <= 0) return;
    std::wstring wideText(static_cast<std::size_t>(wideLength), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wideText.data(), wideLength) != wideLength) return;
    DrawTextW(dc, wideText.data(), wideLength, &rect, format | DT_NOPREFIX);
}

inline void TextW(HDC dc, RECT rect, std::wstring_view text, COLORREF color, UINT format = DT_CENTER | DT_VCENTER | DT_SINGLELINE) {
    rect.top += kTextBaselineOffsetY;
    rect.bottom += kTextBaselineOffsetY;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.data(), static_cast<int>(text.size()), &rect, format | DT_NOPREFIX);
}

[[nodiscard]] inline Gdiplus::Color GdiplusColor(COLORREF color) noexcept {
    return Gdiplus::Color(
        255,
        GetRValue(color),
        GetGValue(color),
        GetBValue(color));
}

inline void AddRoundedRectPath(
    Gdiplus::GraphicsPath& path,
    const Gdiplus::RectF& rect,
    float radius) {
    const float diameter = std::min(
        std::min(rect.Width, rect.Height),
        std::max(0.0F, radius * 2.0F));
    if (diameter <= 1.0F) {
        path.AddRectangle(rect);
        return;
    }
    path.AddArc(rect.X, rect.Y, diameter, diameter, 180.0F, 90.0F);
    path.AddArc(rect.X + rect.Width - diameter, rect.Y, diameter, diameter, 270.0F, 90.0F);
    path.AddArc(rect.X + rect.Width - diameter, rect.Y + rect.Height - diameter, diameter, diameter, 0.0F, 90.0F);
    path.AddArc(rect.X, rect.Y + rect.Height - diameter, diameter, diameter, 90.0F, 90.0F);
    path.CloseFigure();
}

inline void AddTopRoundedRectPath(
    Gdiplus::GraphicsPath& path,
    const Gdiplus::RectF& rect,
    float radius) {
    const float diameter = std::min(
        std::min(rect.Width, rect.Height * 2.0F),
        std::max(0.0F, radius * 2.0F));
    if (diameter <= 1.0F) {
        path.AddRectangle(rect);
        return;
    }
    path.AddArc(rect.X, rect.Y, diameter, diameter, 180.0F, 90.0F);
    path.AddArc(rect.X + rect.Width - diameter, rect.Y, diameter, diameter, 270.0F, 90.0F);
    path.AddLine(rect.X + rect.Width, rect.Y + diameter * 0.5F, rect.X + rect.Width, rect.Y + rect.Height);
    path.AddLine(rect.X + rect.Width, rect.Y + rect.Height, rect.X, rect.Y + rect.Height);
    path.AddLine(rect.X, rect.Y + rect.Height, rect.X, rect.Y + diameter * 0.5F);
    path.CloseFigure();
}

inline void ConfigureSectionGraphics(Gdiplus::Graphics& graphics) {
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
}

inline void DrawRoundedFrame(
    HDC dc,
    const RECT& rect,
    COLORREF fill,
    COLORREF border,
    float cornerRadius,
    float outlineWidth,
    COLORREF accent = CLR_INVALID,
    float accentWidth = 0.0F) {
    if (dc == nullptr || rect.right - rect.left <= 2 || rect.bottom - rect.top <= 2) {
        return;
    }
    HeroIconGdiplusRuntime::EnsureStarted();
    Gdiplus::Graphics graphics(dc);
    ConfigureSectionGraphics(graphics);
    Gdiplus::GraphicsPath path;
    AddRoundedRectPath(
        path,
        Gdiplus::RectF{
            static_cast<Gdiplus::REAL>(rect.left) + 0.5F,
            static_cast<Gdiplus::REAL>(rect.top) + 0.5F,
            static_cast<Gdiplus::REAL>(rect.right - rect.left) - 1.0F,
            static_cast<Gdiplus::REAL>(rect.bottom - rect.top) - 1.0F,
        },
        cornerRadius);
    Gdiplus::SolidBrush brush(GdiplusColor(fill));
    graphics.FillPath(&brush, &path);
    if (accent != CLR_INVALID && accentWidth > 0.0F) {
        const Gdiplus::GraphicsState state = graphics.Save();
        graphics.SetClip(&path, Gdiplus::CombineModeIntersect);
        Gdiplus::SolidBrush accentBrush(GdiplusColor(accent));
        graphics.FillRectangle(
            &accentBrush,
            Gdiplus::RectF{
                static_cast<Gdiplus::REAL>(rect.left) + 0.5F,
                static_cast<Gdiplus::REAL>(rect.top) + 0.5F,
                accentWidth,
                static_cast<Gdiplus::REAL>(rect.bottom - rect.top) - 1.0F,
            });
        graphics.Restore(state);
    }
    Gdiplus::Pen pen(GdiplusColor(border), outlineWidth);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    graphics.DrawPath(&pen, &path);
}

inline void DrawInputFrame(HDC dc, const RECT& rect, COLORREF fill, COLORREF border) {
    DrawRoundedFrame(dc, rect, fill, border, kInputCornerRadius, kInputOutlineWidth);
}

inline void DrawSectionButtonFrame(HDC dc, const RECT& rect, COLORREF fill, COLORREF border) {
    DrawRoundedFrame(dc, rect, fill, border, kSectionCornerRadius, kSectionOutlineWidth);
}

inline void DrawSectionAccentFrame(HDC dc, const RECT& rect, COLORREF fill, COLORREF border, COLORREF accent) {
    DrawRoundedFrame(dc, rect, fill, border, kSectionCornerRadius, kSectionOutlineWidth, accent, 3.5F);
}

inline void FillSectionHeaderBackground(HDC dc, const RECT& rect, COLORREF fill) {
    if (dc == nullptr || rect.right - rect.left <= 2 || rect.bottom - rect.top <= 2) {
        return;
    }
    HeroIconGdiplusRuntime::EnsureStarted();
    Gdiplus::Graphics graphics(dc);
    ConfigureSectionGraphics(graphics);
    Gdiplus::GraphicsPath path;
    AddTopRoundedRectPath(
        path,
        Gdiplus::RectF{
            static_cast<Gdiplus::REAL>(rect.left) + 1.0F,
            static_cast<Gdiplus::REAL>(rect.top) + 1.0F,
            static_cast<Gdiplus::REAL>(rect.right - rect.left) - 2.0F,
            static_cast<Gdiplus::REAL>(rect.bottom - rect.top) - 1.0F,
        },
        kSectionCornerRadius);
    Gdiplus::SolidBrush brush(GdiplusColor(fill));
    graphics.FillPath(&brush, &path);
}

inline void DrawSectionCardOutline(HDC dc, const RECT& rect, const EditorTheme& theme) {
    if (dc == nullptr || rect.right - rect.left <= 2 || rect.bottom - rect.top <= 2) {
        return;
    }
    HeroIconGdiplusRuntime::EnsureStarted();
    Gdiplus::Graphics graphics(dc);
    ConfigureSectionGraphics(graphics);
    Gdiplus::GraphicsPath path;
    AddRoundedRectPath(
        path,
        Gdiplus::RectF{
            static_cast<Gdiplus::REAL>(rect.left) + 1.0F,
            static_cast<Gdiplus::REAL>(rect.top) + 1.0F,
            static_cast<Gdiplus::REAL>(rect.right - rect.left) - 2.0F,
            static_cast<Gdiplus::REAL>(rect.bottom - rect.top) - 2.0F,
        },
        kSectionCornerRadius);
    Gdiplus::Pen pen(GdiplusColor(Color(theme.borderPanel)), kSectionOutlineWidth);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    graphics.DrawPath(&pen, &path);
}

inline void DrawDivider(HDC dc, const EditorTheme& theme, int left, int right, int y) {
    const int insetLeft = std::min(right, left + kSectionDividerInset);
    const int insetRight = std::max(insetLeft, right - kSectionDividerInset);
    GdiDrawing::FillRectColor(dc, Rect(insetLeft, y, insetRight, y + kDividerHeight), Color(theme.borderChrome));
}

inline void DrawTriangle(HDC dc, RECT rect, bool expanded, COLORREF color) {
    ProjectFilesPanelDrawing::DrawDisclosureTriangle(dc, rect, color, expanded);
}

} // namespace kb::editor::panel_style
