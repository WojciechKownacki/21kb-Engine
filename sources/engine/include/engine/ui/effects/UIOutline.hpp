#pragma once

#include "engine/math/EngineMath.hpp"

#include <cstdint>
#include <string_view>

namespace kb::scene {

struct UIOutline {
    static constexpr std::string_view StableId = "kb21.ui.outline";
    static constexpr std::uint32_t SchemaVersion = 1U;

    kb::math::Color color{0.0F, 0.0F, 0.0F, 1.0F};
    float width = 1.0F;
};

} // namespace kb::scene
