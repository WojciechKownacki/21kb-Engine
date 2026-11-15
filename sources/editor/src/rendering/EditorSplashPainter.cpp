#include "rendering/EditorSplashPainter.hpp"

#if defined(_WIN32)
#include "platform/win32/Utf8Gdi.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconGdiplusRuntime.hpp"
#include "rendering/gdi/EditorFontCache.hpp"

#pragma warning(push, 0)
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>
#pragma warning(pop)

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

namespace kb::editor {
namespace {

constexpr std::string_view kBrandName = "21kb Engine";
constexpr std::string_view kBrandMark = "21";

[[nodiscard]] int Scaled(int logical, int dpi) noexcept {
    return MulDiv(logical, dpi, 96);
}

[[nodiscard]] Gdiplus::Color ToGdiplus(EditorColor color) noexcept {
    return Gdiplus::Color(255, color.r, color.g, color.b);
}

void RoundedPath(Gdiplus::GraphicsPath& path, const RECT& rect, int radius) {
    const float diameter = static_cast<float>(std::min<int>({ radius * 2, static_cast<int>(rect.right - rect.left),
        static_cast<int>(rect.bottom - rect.top) }));
    const float left = static_cast<float>(rect.left);
    const float top = static_cast<float>(rect.top);
    const float right = static_cast<float>(rect.right);
    const float bottom = static_cast<float>(rect.bottom);
    path.AddArc(left, top, diameter, diameter, 180.0F, 90.0F);
    path.AddArc(right - diameter, top, diameter, diameter, 270.0F, 90.0F);
    path.AddArc(right - diameter, bottom - diameter, diameter, diameter, 0.0F, 90.0F);
    path.AddArc(left, bottom - diameter, diameter, diameter, 90.0F, 90.0F);
    path.CloseFigure();
}

void FillRounded(Gdiplus::Graphics& graphics, const RECT& rect, int radius, EditorColor color) {
    if (rect.right <= rect.left || rect.bottom <= rect.top) {
        return;
    }
    Gdiplus::GraphicsPath path;
    RoundedPath(path, rect, radius);
    const Gdiplus::SolidBrush brush(ToGdiplus(color));
    graphics.FillPath(&brush, &path);
}

void DrawLine(HDC dc, RECT rect, std::string_view text, EditorColor color, int fontPixels, int weight, UINT align) {
    if (text.empty()) {
        return;
    }
    const HGDIOBJ previous = SelectObject(dc, EditorFontCache::Acquire(fontPixels, weight));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GdiDrawing::ToColorRef(color));
    DrawTextUtf8(dc, text.data(), static_cast<int>(text.size()), &rect,
        align | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, previous);
}

} // namespace

EditorSplashLayout EditorSplashPainter::Layout(int dpi) noexcept {
    const auto px = [dpi](int logical) { return Scaled(logical, dpi); };
    EditorSplashLayout layout;
    layout.client = RECT{ 0, 0, px(kLogicalWidth), px(kLogicalHeight) };
    const int pad = px(36);
    const int markSize = px(56);
    layout.mark = RECT{ pad, pad, pad + markSize, pad + markSize };
    layout.title = RECT{ layout.mark.right + px(18), pad, layout.client.right - pad, pad + markSize };
    const int stageTop = px(150);
    const int stageHeight = px(26);
    layout.percent = RECT{ layout.client.right - pad - px(48), stageTop, layout.client.right - pad, stageTop + stageHeight };
    layout.stage = RECT{ pad, stageTop, layout.percent.left - px(12), stageTop + stageHeight };
    layout.track = RECT{ pad, px(188), layout.client.right - pad, px(188) + px(6) };
    layout.detail = RECT{ pad, px(204), layout.client.right - pad, px(204) + px(22) };
    layout.markFont = px(22);
    layout.titleFont = px(28);
    layout.stageFont = px(17);
    layout.detailFont = px(13);
    layout.border = std::max(1, px(1));
    return layout;
}

int EditorSplashPainter::FillWidth(const EditorSplashLayout& layout, double fraction) noexcept {
    const int width = static_cast<int>(layout.track.right - layout.track.left);
    const double clamped = std::clamp(fraction, 0.0, 1.0);
    return clamped >= 1.0 ? width : static_cast<int>(std::floor(static_cast<double>(width) * clamped));
}

void EditorSplashPainter::Paint(HDC dc, const EditorSplashLayout& layout, const EditorTheme& theme,
    const EditorStartupSnapshot& snapshot) {
    GdiDrawing::DrawSharpFrame(dc, layout.client, GdiDrawing::ToColorRef(theme.background),
        GdiDrawing::ToColorRef(theme.borderPanel));

    HeroIconGdiplusRuntime::EnsureStarted();
    {
        Gdiplus::Graphics graphics(dc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        FillRounded(graphics, layout.mark, (layout.mark.right - layout.mark.left) / 5, theme.accent);
        const int trackHeight = static_cast<int>(layout.track.bottom - layout.track.top);
        FillRounded(graphics, layout.track, trackHeight / 2, theme.header);
        const int fill = FillWidth(layout, snapshot.fraction);
        if (fill > 0) {
            // A fill shorter than the bar is tall is drawn that wide, so its ends stay round.
            RECT filled = layout.track;
            filled.right = filled.left + std::max(fill, trackHeight);
            FillRounded(graphics, filled, trackHeight / 2, theme.accent);
        }
    }

    DrawLine(dc, layout.mark, kBrandMark, theme.textPrimary, layout.markFont, FW_BOLD, DT_CENTER);
    DrawLine(dc, layout.title, kBrandName, theme.textPrimary, layout.titleFont, FW_SEMIBOLD, DT_LEFT);
    DrawLine(dc, layout.stage, snapshot.stage, theme.textPrimary, layout.stageFont, FW_SEMIBOLD, DT_LEFT);
    const int percent = static_cast<int>(std::floor(std::clamp(snapshot.fraction, 0.0, 1.0) * 100.0));
    DrawLine(dc, layout.percent, std::to_string(percent) + "%", theme.textSecondary, layout.detailFont, FW_NORMAL,
        DT_RIGHT);
    DrawLine(dc, layout.detail, snapshot.detail, theme.textSecondary, layout.detailFont, FW_NORMAL, DT_LEFT);
}

} // namespace kb::editor

#endif
