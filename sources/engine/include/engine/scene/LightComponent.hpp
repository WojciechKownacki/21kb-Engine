#pragma once

#include "engine/scene/TransformComponent.hpp"

#include <cstdint>

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
    // LIB-141: when true, the renderer tints `color` by a blackbody-radiator RGB derived from
    // colorTemperatureKelvin (kb::render::SceneLightColor::Resolve - kb::scene cannot host the
    // math itself since it must stay renderer-agnostic, same reasoning as
    // kb::render::SceneTransformMatrices deriving render matrices from TransformComponent).
    // False by default so existing content's authored `color` is used unmodified.
    bool useColorTemperature = false;
    // Degrees Kelvin, meaningful only when useColorTemperature is true. 6500K approximates
    // daylight-neutral white (blackbody RGB ~= {1,1,1}, so enabling the toggle at the default
    // value is a near no-op tint).
    float colorTemperatureKelvin = 6500.0F;
    // LIB-141: bitmask mirroring MeshRendererComponent::layer's exact role, but for whether a
    // CAMERA's cullingMask allows this light to contribute to that camera's forward lighting
    // (kb::render::SceneForwardLightSelector) - the light-side equivalent of "which layer do I
    // render on". Default 1U (layer 1) matches MeshRendererComponent::layer's own default, so
    // existing content (whose cameras default to cullingMask=all-bits, which already includes
    // layer 1) sees no behavior change.
    std::uint32_t layerMask = 1U;
};

} // namespace kb::scene
