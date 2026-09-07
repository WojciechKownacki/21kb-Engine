#pragma once

#include "engine/math/EngineMath.hpp"

namespace kb::scene {

struct UIEffects {
    bool clipChildren = false;
    bool mask = false;
    bool shadowEnabled = false;
    kb::math::Vec2 shadowOffset{2.0F, 2.0F};
    kb::math::Color shadowColor{0.0F, 0.0F, 0.0F, 0.5F};
    float shadowBlur = 0.0F;
    bool outlineEnabled = false;
    kb::math::Color outlineColor{0.0F, 0.0F, 0.0F, 1.0F};
    float outlineWidth = 1.0F;
    float backgroundBlur = 0.0F;
};

} // namespace kb::scene
