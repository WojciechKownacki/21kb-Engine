#pragma once

#include "engine/math/EngineMath.hpp"
#include "engine/ui/layout/UIEdges.hpp"

namespace kb::scene {

struct UIPaint {
    kb::math::Color backgroundColor{0.0F, 0.0F, 0.0F, 0.0F};
    kb::math::Color borderColor{0.0F, 0.0F, 0.0F, 0.0F};
    UIEdges borderWidth{};
    kb::math::Vec4 cornerRadius{};
    float opacity = 1.0F;
};

} // namespace kb::scene
