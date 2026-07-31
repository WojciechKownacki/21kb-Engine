#pragma once

#include "engine/scene/TransformComponent.hpp"

#include <cmath>
#include <cstdint>
#include <string_view>

namespace kb::scene {

// The visible background is a scene-global rendering policy. Multiple entities
// may author it; the highest priority, then lowest entity id, is selected by
// the runtime deterministically.
enum class WorldBackdropMode : std::uint8_t {
    SolidColor,
    VerticalGradient,
    EnvironmentMap,
    ProceduralSky,
};

struct WorldBackdropComponent {
    static constexpr std::string_view StableId = "kb21.world.backdrop";
    static constexpr std::uint32_t SchemaVersion = 1U;

    WorldBackdropMode mode = WorldBackdropMode::SolidColor;
    Vec3 color{ 0.0F, 0.0F, 0.0F };
    Vec3 horizonColor{ 0.16F, 0.20F, 0.28F };
    Vec3 zenithColor{ 0.36F, 0.46F, 0.66F };
    // RenderTexture asset containing a 2D equirectangular environment image.
    std::uint64_t environmentAssetId = 0U;
    float horizonHeight = 0.0F;
    float gradientExponent = 1.0F;
    std::int32_t priority = 0;
    bool enabled = true;
};

[[nodiscard]] constexpr bool IsWorldBackdropModeValid(WorldBackdropMode value) noexcept {
    return value == WorldBackdropMode::SolidColor || value == WorldBackdropMode::VerticalGradient ||
        value == WorldBackdropMode::EnvironmentMap || value == WorldBackdropMode::ProceduralSky;
}

[[nodiscard]] inline bool IsWorldBackdropComponentValid(const WorldBackdropComponent& value) noexcept {
    return IsWorldBackdropModeValid(value.mode) &&
        std::isfinite(value.color.x) && std::isfinite(value.color.y) && std::isfinite(value.color.z) &&
        std::isfinite(value.horizonColor.x) && std::isfinite(value.horizonColor.y) && std::isfinite(value.horizonColor.z) &&
        std::isfinite(value.zenithColor.x) && std::isfinite(value.zenithColor.y) && std::isfinite(value.zenithColor.z) &&
        std::isfinite(value.horizonHeight) && std::isfinite(value.gradientExponent) && value.gradientExponent > 0.0F;
}

} // namespace kb::scene
