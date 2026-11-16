#pragma once

#include <cmath>
#include <cstdint>
#include <string_view>

namespace kb::scene {

// A flat mirror: a rectangle in the object's own XZ plane (its normal is the object's up axis) that shows the
// scene reflected in it. Surfaces lying on the rectangle reflect the mirrored scene; everywhere else they keep
// their usual reflections. The renderer draws at most a few mirrors per frame, the most visible first.
struct PlanarReflectionComponent {
    static constexpr std::string_view StableId = "kb21.scene.planar-reflection";
    static constexpr std::uint32_t SchemaVersion = 1U;

    // Size of the mirror in metres along the object's X and Z axes.
    float width = 10.0F;
    float depth = 10.0F;
    // Size of the reflection picture relative to the screen (0.25 sharp but costly, 0.5 is a good balance).
    float resolutionScale = 0.5F;
    // Which layers show up in the reflection (bit mask, every layer by default).
    std::uint32_t reflectedLayers = 0xFFFFFFFFU;
    // Objects farther than this many metres from the mirror's picture point of view are left out.
    float maxDistance = 100.0F;
    // Whether the reflected picture gets shadows (costs an extra shadow pass).
    bool reflectShadows = false;
    bool enabled = true;
    bool operator==(const PlanarReflectionComponent&) const = default;
};

[[nodiscard]] inline bool IsPlanarReflectionComponentValid(const PlanarReflectionComponent& value) noexcept {
    return std::isfinite(value.width) && value.width > 0.0F && value.width <= 1.0e6F &&
        std::isfinite(value.depth) && value.depth > 0.0F && value.depth <= 1.0e6F &&
        std::isfinite(value.resolutionScale) && value.resolutionScale >= 0.1F && value.resolutionScale <= 1.0F &&
        std::isfinite(value.maxDistance) && value.maxDistance >= 1.0F && value.maxDistance <= 1.0e6F;
}

} // namespace kb::scene
