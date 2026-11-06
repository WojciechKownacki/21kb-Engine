#pragma once

#include "engine/scene/TransformComponent.hpp"

#include <cmath>
#include <cstdint>
#include <string_view>

namespace kb::scene {

enum class LightKind {
    Directional,
    Point,
    Spot,
    AreaRect,
    AreaDisk,
    Tube
};

// Light Mobility: a Static light's shadow maps are cached and
// redrawn only when something in its reach changes; a Movable one is redrawn every frame.
enum class LightMobility {
    Movable,
    Static
};

// How a light's shadow is filtered; Default is the project's filter. Pcss widens the penumbra with the light's size.
enum class LightShadowFilter : std::uint32_t {
    Default,
    Hard,
    Pcf,
    Pcss
};

// The resolution of a directional light's shadow maps relative to the project's: Low half, Medium three quarters,
// High the project's own, Ultra twice. Local lights share one atlas and keep the project's.
enum class LightShadowQuality : std::uint32_t {
    Default,
    Low,
    Medium,
    High,
    Ultra
};

struct LightComponent {
    static constexpr std::string_view StableId = "kb21.light.d3.radiance-emitter";
    static constexpr std::uint32_t SchemaVersion = 1U;

    LightKind kind = LightKind::Point;
    Vec3 color{ 1.0F, 1.0F, 1.0F };
    float intensity = 1.0F;
    float range = 10.0F;
    float innerConeDegrees = 25.0F;
    float outerConeDegrees = 35.0F;
    float areaWidth = 1.0F;
    float areaHeight = 1.0F;
    float contactShadowLength = 0.0F;
    float volumetricScattering = 1.0F;  // Volumetric Scattering default
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
    // Size of the emitter for soft shadows (PCSS): metres (Source Radius); a directional
    // light's Source Angle in degrees (0.5357 = the sun).
    float sourceRadius = 0.0F;
    float shadowStrength = 1.0F;   // 0..1, Shadow Strength
    float shadowDistance = 0.0F;   // directional, m (Shadow Distance); 0 = the project's
    LightMobility mobility = LightMobility::Movable;
    // Shadow bias in texels of the shadow map along the light, and the offset of the lookup along the surface
    // normal in texels; 0 = the project's. A thin object far from the light needs less than the default.
    float shadowBias = 0.0F;
    float shadowNormalBias = 0.0F;
    LightShadowFilter shadowFilter = LightShadowFilter::Default;
    LightShadowQuality shadowQuality = LightShadowQuality::Default;
    // Directional: how many cascades the sun's shadow uses (1..4) and how much wider each one reaches than the
    // finer one below it; 0 = the project's count and the default ratio of 4.
    std::uint32_t shadowCascadeCount = 0U;
    float shadowCascadeDistribution = 0.0F;
};

[[nodiscard]] constexpr bool IsLightKindValid(LightKind value) noexcept {
    return value == LightKind::Directional || value == LightKind::Point || value == LightKind::Spot ||
        value == LightKind::AreaRect || value == LightKind::AreaDisk || value == LightKind::Tube;
}

[[nodiscard]] inline bool IsLightComponentValid(const LightComponent& value) noexcept {
    return IsLightKindValid(value.kind) &&
        std::isfinite(value.color.x) && std::isfinite(value.color.y) && std::isfinite(value.color.z) &&
        std::isfinite(value.intensity) && std::isfinite(value.range) &&
        std::isfinite(value.innerConeDegrees) && std::isfinite(value.outerConeDegrees) &&
        std::isfinite(value.areaWidth) && std::isfinite(value.areaHeight) &&
        std::isfinite(value.contactShadowLength) && std::isfinite(value.volumetricScattering) &&
        std::isfinite(value.colorTemperatureKelvin) &&
        std::isfinite(value.sourceRadius) && std::isfinite(value.shadowStrength) &&
        std::isfinite(value.shadowDistance) &&
        (value.mobility == LightMobility::Movable || value.mobility == LightMobility::Static) &&
        std::isfinite(value.shadowBias) && value.shadowBias >= 0.0F && value.shadowBias <= 16.0F &&
        std::isfinite(value.shadowNormalBias) && value.shadowNormalBias >= 0.0F && value.shadowNormalBias <= 16.0F &&
        value.shadowFilter <= LightShadowFilter::Pcss && value.shadowQuality <= LightShadowQuality::Ultra &&
        value.shadowCascadeCount <= 4U &&
        std::isfinite(value.shadowCascadeDistribution) && value.shadowCascadeDistribution >= 0.0F &&
        value.shadowCascadeDistribution <= 16.0F && (value.shadowCascadeDistribution == 0.0F || value.shadowCascadeDistribution >= 1.5F);
}

} // namespace kb::scene
