#pragma once

#include <cstdint>
#include <string_view>

namespace kb::scene {

// The points themselves stay in the shared GuideCurveComponent on this entity.
// This component only owns the authored rendering policy for that curve.
enum class SpaceStrokeMode : std::uint8_t {
    Polyline = 0U,
    Spline = 1U,
    Beam = 2U,
    Cable = 3U,
};

struct SpaceStrokeComponent {
    static constexpr std::string_view StableId = "kb21.draw.d3.space-stroke";
    static constexpr std::uint32_t SchemaVersion = 1U;

    std::uint64_t meshAssetId = 0U;
    std::uint64_t materialAssetId = 0U;
    SpaceStrokeMode mode = SpaceStrokeMode::Polyline;
    float width = 0.1F;
    float cableSag = 0.0F;
    std::uint8_t splineSegments = 8U;
    std::uint32_t layer = 1U;
    bool castsShadow = false;
    bool receivesShadow = true;
    bool enabled = false;
};

[[nodiscard]] constexpr bool IsSpaceStrokeModeValid(SpaceStrokeMode value) noexcept {
    return value == SpaceStrokeMode::Polyline || value == SpaceStrokeMode::Spline ||
        value == SpaceStrokeMode::Beam || value == SpaceStrokeMode::Cable;
}

[[nodiscard]] constexpr bool IsSpaceStrokeComponentValid(const SpaceStrokeComponent& value) noexcept {
    return value.meshAssetId != 0U && value.width > 0.0F && value.width <= 100000.0F &&
        value.cableSag >= 0.0F && value.cableSag <= 100000.0F &&
        value.splineSegments >= 2U && value.splineSegments <= 32U && value.layer != 0U &&
        IsSpaceStrokeModeValid(value.mode);
}

[[nodiscard]] constexpr bool IsSpaceStrokeComponentPersistable(const SpaceStrokeComponent& value) noexcept {
    return IsSpaceStrokeComponentValid(value) ||
        (!value.enabled && value.width > 0.0F && value.width <= 100000.0F &&
            value.cableSag >= 0.0F && value.cableSag <= 100000.0F &&
            value.splineSegments >= 2U && value.splineSegments <= 32U && value.layer != 0U &&
            IsSpaceStrokeModeValid(value.mode));
}

} // namespace kb::scene
