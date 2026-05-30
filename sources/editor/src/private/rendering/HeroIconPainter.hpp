#pragma once

#include "rendering/HeroIconKind.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

class HeroIconPainter {
public:
    HeroIconPainter() = delete;

    static void Draw(HDC dc, const RECT& rect, HeroIconKind icon, COLORREF color, int strokeWidth = 1);
};

#endif

} // namespace kb::editor
