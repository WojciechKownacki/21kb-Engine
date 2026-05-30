#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

struct HeroIconDrawFrame {
    float left = 0.0F;
    float top = 0.0F;
    float scale = 1.0F;

    [[nodiscard]] static HeroIconDrawFrame FromRect(const RECT& rect, float viewBoxSize) noexcept;
};

#endif

} // namespace kb::editor
