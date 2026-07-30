#pragma once

#include "engine/scene/TransformComponent.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace kb::scene {

// Authoring geometry only. Systems that consume a curve retain their own
// traversal state; the component is safe to share between movement, AI,
// rendering and editor tools.
enum class GuideCurveInterpolation : std::uint8_t {
    Linear = 0U,
    CatmullRom = 1U,
};

struct GuideCurveComponent {
    static constexpr std::string_view StableId = "kb21.scene.guide-curve";
    static constexpr std::uint32_t SchemaVersion = 1U;
    static constexpr std::uint32_t MaxControlPoints = 8U;

    std::array<Vec3, MaxControlPoints> controlPoints{
        Vec3{ -0.5F, 0.0F, 0.0F }, Vec3{ 0.5F, 0.0F, 0.0F }
    };
    std::uint32_t controlPointCount = 2U;
    GuideCurveInterpolation interpolation = GuideCurveInterpolation::CatmullRom;
    bool closed = false;
    bool enabled = true;
};

[[nodiscard]] constexpr bool IsGuideCurveInterpolationValid(GuideCurveInterpolation interpolation) noexcept {
    return interpolation == GuideCurveInterpolation::Linear || interpolation == GuideCurveInterpolation::CatmullRom;
}

[[nodiscard]] constexpr bool IsGuideCurveControlPointCountValid(std::uint32_t count) noexcept {
    return count >= 2U && count <= GuideCurveComponent::MaxControlPoints;
}

} // namespace kb::scene
