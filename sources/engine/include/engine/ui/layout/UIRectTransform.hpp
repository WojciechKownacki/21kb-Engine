#pragma once

#include "engine/math/EngineMath.hpp"

#include <cstdint>

namespace kb::scene {

struct UIRectTransform {
    kb::math::Vec2 anchorMin{};
    kb::math::Vec2 anchorMax{};
    kb::math::Vec2 offsetMin{};
    kb::math::Vec2 offsetMax{100.0F, 100.0F};
    kb::math::Vec2 pivot{0.5F, 0.5F};
    kb::math::Vec2 scale{1.0F, 1.0F};
    float rotationDegrees = 0.0F;
    std::int32_t zOrder = 0;
};

} // namespace kb::scene
