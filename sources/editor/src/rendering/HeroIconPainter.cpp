#include "rendering/HeroIconPainter.hpp"

#include "rendering/HeroIconCatalog.hpp"
#include "rendering/HeroIconDrawFrame.hpp"
#include "rendering/HeroIconGdiplusRuntime.hpp"
#include "rendering/HeroIconPathPainter.hpp"
#include "rendering/SvgGraphicsPathBuilder.hpp"

#if defined(_WIN32)
#pragma warning(push, 0)
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>
#pragma warning(pop)
#endif

#include <algorithm>

namespace kb::editor {

#if defined(_WIN32)
namespace {

[[nodiscard]] Gdiplus::Color ToGdiplusColor(COLORREF color) noexcept {
    return Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color));
}

[[nodiscard]] float ResolveStrokeWidth(const HeroIconGlyph& glyph, int strokeWidth) noexcept {
    return glyph.strokeWidth > 0.0F ? glyph.strokeWidth : static_cast<float>(std::max(1, strokeWidth));
}

} // namespace

void HeroIconPainter::Draw(HDC dc, const RECT& rect, HeroIconKind icon, COLORREF color, int strokeWidth) {
    if (dc == nullptr || rect.right <= rect.left || rect.bottom <= rect.top) {
        return;
    }

    const HeroIconGlyph glyph = HeroIconCatalog::Glyph(icon);
    if (glyph.paths.empty()) {
        return;
    }

    HeroIconGdiplusRuntime::EnsureStarted();

    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    const HeroIconDrawFrame frame = HeroIconDrawFrame::FromRect(rect, glyph.viewBoxSize);
    Gdiplus::Matrix transform;
    transform.Translate(frame.left, frame.top);
    transform.Scale(frame.scale, frame.scale);
    graphics.SetTransform(&transform);

    const Gdiplus::Color iconColor = ToGdiplusColor(color);
    const float effectiveStrokeWidth = ResolveStrokeWidth(glyph, strokeWidth);

    for (const HeroIconPath& pathData : glyph.paths) {
        Gdiplus::GraphicsPath path(pathData.filled ? Gdiplus::FillModeAlternate : Gdiplus::FillModeWinding);
        SvgGraphicsPathBuilder(pathData.data).Build(path);
        HeroIconPathPainter::Paint(graphics, path, pathData, iconColor, effectiveStrokeWidth);
    }

    graphics.ResetTransform();
}

#endif

} // namespace kb::editor
