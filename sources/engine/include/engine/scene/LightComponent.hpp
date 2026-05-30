#pragma once

#include "engine/scene/TransformComponent.hpp"

namespace kb::scene {

enum class LightKind {
    Directional,
    Point,
    Spot
};

struct LightComponent {
    LightKind kind = LightKind::Point;
    Vec3 color{ 1.0F, 1.0F, 1.0F };
    float intensity = 1.0F;
    float range = 10.0F;
    float innerConeDegrees = 25.0F;
    float outerConeDegrees = 35.0F;
};

} // namespace kb::scene
