#pragma once

#include "engine/scene/TransformComponent.hpp"

namespace kb::scene {

enum class LightKind {
    Directional,
    Point,
    Spot,
    AreaRect,
    AreaDisk,
    Tube
};

struct LightComponent {
    LightKind kind = LightKind::Point;
    Vec3 color{ 1.0F, 1.0F, 1.0F };
    float intensity = 1.0F;
    float range = 10.0F;
    float innerConeDegrees = 25.0F;
    float outerConeDegrees = 35.0F;
    float areaWidth = 1.0F;
    float areaHeight = 1.0F;
    float contactShadowLength = 0.0F;
    float volumetricScattering = 0.0F;
    bool castsShadow = true;
};

} // namespace kb::scene
