#pragma once

#include "engine/math/EngineMath.hpp"
#include "engine/ui/layout/UIEdges.hpp"

#include <cstdint>

namespace kb::scene {

enum class UIAlignment : std::uint8_t {
    Start,
    Center,
    End,
    Stretch,
};

enum class UIContainerLayoutMode : std::uint8_t {
    None,
    Overlay,
    Horizontal,
    Vertical,
    Grid,
    Wrap,
};

struct UIContainerLayout {
    UIContainerLayoutMode mode = UIContainerLayoutMode::None;
    UIEdges padding{};
    kb::math::Vec2 spacing{};
    UIAlignment horizontalAlignment = UIAlignment::Start;
    UIAlignment verticalAlignment = UIAlignment::Start;
    kb::math::Vec2 cellSize{100.0F, 100.0F};
    std::uint32_t columns = 1U;
};

} // namespace kb::scene
