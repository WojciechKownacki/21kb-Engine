#pragma once

#include "engine/math/EngineMath.hpp"

#include <cstdint>
#include <string_view>

namespace kb::scene {

struct UIShadow {
    static constexpr std::string_view StableId = "kb21.ui.shadow";
    static constexpr std::uint32_t SchemaVersion = 1U;

    kb::math::Vec2 offset{2.0F, 2.0F};
    kb::math::Color color{0.0F, 0.0F, 0.0F, 0.5F};
    float blur = 0.0F;
};

} // namespace kb::scene
