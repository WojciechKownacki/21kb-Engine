#pragma once

#include "engine/scene/TransformComponent.hpp"

#include <cmath>
#include <cstdint>
#include <string_view>

namespace kb::scene {

// Scene-global indirect-lighting policy. Several entities may author one;
// runtime deterministically selects highest priority, then lowest entity id.
// ComponentReflection::Enum32 serializes and mutates enum fields as int32.
// Keep the ECS field representation identical to that contract.
enum class AmbientRadianceMode : std::int32_t {
    Constant,
    Gradient,
    EnvironmentMap,
    ProceduralSky,
    CapturedEnvironment,
    EstimatedEnvironment,
};

struct AmbientRadianceComponent {
    static constexpr std::string_view StableId = "kb21.light.ambient-radiance";
    static constexpr std::uint32_t SchemaVersion = 1U;

    AmbientRadianceMode mode = AmbientRadianceMode::Constant;
    Vec3 color{ 0.18F, 0.20F, 0.23F };
    Vec3 horizonColor{ 0.16F, 0.20F, 0.28F };
    Vec3 zenithColor{ 0.36F, 0.46F, 0.66F };
    // A linear equirectangular RenderTexture for EnvironmentMap mode.
    std::uint64_t environmentAssetId = 0U;
    float intensity = 1.0F;
    float diffuseIntensity = 1.0F;
    float specularIntensity = 0.25F;
    std::int32_t priority = 0;
    bool enabled = true;
};

[[nodiscard]] constexpr bool IsAmbientRadianceModeValid(AmbientRadianceMode value) noexcept {
    return value == AmbientRadianceMode::Constant || value == AmbientRadianceMode::Gradient ||
        value == AmbientRadianceMode::EnvironmentMap || value == AmbientRadianceMode::ProceduralSky ||
        value == AmbientRadianceMode::CapturedEnvironment || value == AmbientRadianceMode::EstimatedEnvironment;
}

[[nodiscard]] inline bool IsAmbientRadianceComponentValid(const AmbientRadianceComponent& value) noexcept {
    return IsAmbientRadianceModeValid(value.mode) &&
        std::isfinite(value.color.x) && std::isfinite(value.color.y) && std::isfinite(value.color.z) &&
        std::isfinite(value.horizonColor.x) && std::isfinite(value.horizonColor.y) && std::isfinite(value.horizonColor.z) &&
        std::isfinite(value.zenithColor.x) && std::isfinite(value.zenithColor.y) && std::isfinite(value.zenithColor.z) &&
        std::isfinite(value.intensity) && value.intensity >= 0.0F &&
        std::isfinite(value.diffuseIntensity) && value.diffuseIntensity >= 0.0F &&
        std::isfinite(value.specularIntensity) && value.specularIntensity >= 0.0F;
}

} // namespace kb::scene
