#pragma once

#include "engine/scene/TransformComponent.hpp"

#include <cstdint>
#include <string_view>

namespace kb::scene {

// A facing panel only describes how this entity's existing flat mesh is
// oriented at render time. It never owns geometry or writes TransformComponent.
enum class FacingPanelMode : std::uint8_t {
    View = 0U,
    Point = 1U,
    Axis = 2U,
    Fixed = 3U,
};

struct FacingPanelComponent {
    static constexpr std::string_view StableId = "kb21.draw.d3.facing-panel";
    static constexpr std::uint32_t SchemaVersion = 1U;

    FacingPanelMode mode = FacingPanelMode::View;
    Vec3 targetPoint{};
    Vec3 axis{ 0.0F, 0.0F, 1.0F };
    Vec3 up{ 0.0F, 1.0F, 0.0F };
    bool enabled = false;
};

[[nodiscard]] constexpr bool IsFacingPanelModeValid(FacingPanelMode value) noexcept {
    return value == FacingPanelMode::View || value == FacingPanelMode::Point || value == FacingPanelMode::Axis || value == FacingPanelMode::Fixed;
}

[[nodiscard]] constexpr float FacingPanelLengthSquared(Vec3 value) noexcept {
    return value.x * value.x + value.y * value.y + value.z * value.z;
}

[[nodiscard]] constexpr bool IsFacingPanelComponentValid(const FacingPanelComponent& value) noexcept {
    if (!IsFacingPanelModeValid(value.mode) || FacingPanelLengthSquared(value.up) <= 0.000001F) return false;
    return value.mode != FacingPanelMode::Axis || FacingPanelLengthSquared(value.axis) > 0.000001F;
}

[[nodiscard]] constexpr bool IsFacingPanelComponentPersistable(const FacingPanelComponent& value) noexcept {
    return IsFacingPanelComponentValid(value);
}

} // namespace kb::scene
