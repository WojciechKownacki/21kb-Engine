#pragma once

#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconCatalog.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <algorithm>
#include <cmath>

namespace kb::editor {

#if defined(_WIN32)

class HeroIconPainter {
public:
    HeroIconPainter() = delete;

    static void Draw(HDC dc, const RECT& rect, HeroIconKind icon, COLORREF color) {
        const HeroIconGlyph glyph = HeroIconCatalog::Glyph(icon);

        ScopedPen pen(1, color);
        HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen.handle));
        HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));

        for (const HeroIconLine& line : glyph.lines) {
            DrawLine(dc, rect, line);
        }
        if (glyph.roundedBox.has_value()) {
            DrawRoundedBox(dc, rect, *glyph.roundedBox);
        }

        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
    }

private:
    [[nodiscard]] static int MapX(const RECT& rect, float value) noexcept {
        return rect.left + static_cast<int>(std::lround(static_cast<float>(rect.right - rect.left) * (value / 24.0F)));
    }

    [[nodiscard]] static int MapY(const RECT& rect, float value) noexcept {
        return rect.top + static_cast<int>(std::lround(static_cast<float>(rect.bottom - rect.top) * (value / 24.0F)));
    }

    static void DrawLine(HDC dc, const RECT& rect, const HeroIconLine& line) {
        MoveToEx(dc, MapX(rect, line.x1), MapY(rect, line.y1), nullptr);
        LineTo(dc, MapX(rect, line.x2), MapY(rect, line.y2));
    }

    static void DrawRoundedBox(HDC dc, const RECT& rect, const HeroIconRoundedBox& box) {
        const int radiusX = std::max(2, static_cast<int>(rect.right - rect.left) / 6);
        const int radiusY = std::max(2, static_cast<int>(rect.bottom - rect.top) / 6);
        RoundRect(
            dc,
            MapX(rect, box.left),
            MapY(rect, box.top),
            MapX(rect, box.right),
            MapY(rect, box.bottom),
            radiusX,
            radiusY);
    }
};

#endif

} // namespace kb::editor
