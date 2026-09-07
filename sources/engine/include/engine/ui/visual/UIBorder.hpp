#pragma once

#include "engine/math/EngineMath.hpp"
#include "engine/ui/layout/UIEdges.hpp"

#include <cstdint>
#include <string_view>

namespace kb::scene {

struct UIBorder {
    static constexpr std::string_view StableId = "kb21.ui.border";
    static constexpr std::uint32_t SchemaVersion = 1U;

    kb::math::Color backgroundColor{0.0F, 0.0F, 0.0F, 0.0F};
    kb::math::Color borderColor{0.0F, 0.0F, 0.0F, 0.0F};
    UIEdges borderWidth{};
    kb::math::Vec4 cornerRadius{};
    float opacity = 1.0F;
};

} // namespace kb::scene
