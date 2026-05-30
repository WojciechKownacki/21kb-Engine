#include "rendering/HeroIconDrawFrame.hpp"

#if defined(_WIN32)

#include <algorithm>

namespace kb::editor {

HeroIconDrawFrame HeroIconDrawFrame::FromRect(const RECT& rect, float viewBoxSize) noexcept {
    const float width = static_cast<float>(rect.right - rect.left);
    const float height = static_cast<float>(rect.bottom - rect.top);
    const float side = std::min(width, height);

    return HeroIconDrawFrame{
        .left = static_cast<float>(rect.left) + (width - side) * 0.5F,
        .top = static_cast<float>(rect.top) + (height - side) * 0.5F,
        .scale = side / viewBoxSize,
    };
}

} // namespace kb::editor

#endif
