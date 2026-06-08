#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::hub {

[[nodiscard]] inline RECT Rect(int left, int top, int right, int bottom) noexcept {
    return RECT{ left, top, right, bottom };
}

[[nodiscard]] inline RECT Inset(RECT rect, int amount) noexcept {
    rect.left += amount;
    rect.top += amount;
    rect.right -= amount;
    rect.bottom -= amount;
    return rect;
}

[[nodiscard]] inline bool Contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

} // namespace kb::hub
