#pragma once

#include <cstdint>

namespace kb::scene {

enum class UIAxisDirection : std::uint8_t {
    LeftToRight,
    RightToLeft,
    BottomToTop,
    TopToBottom,
};

} // namespace kb::scene
