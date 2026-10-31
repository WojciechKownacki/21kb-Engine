#pragma once

#include <cstdint>

namespace kb::render {

// Mobile and Web retain height fog but omit the per-pixel light/shadow ray march.
// These profiles keep the scene's G-buffer requirements; they are not a renderer backend.
enum class SceneFogProfile : std::uint8_t {
    Automatic,
    Desktop,
    Mobile,
    Web,
};

} // namespace kb::render
