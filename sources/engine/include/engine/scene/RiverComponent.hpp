#pragma once

#include "engine/scene/TransformComponent.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace kb::scene {

// One control point of a river, in the local space of its entity: the position is the water surface
// there, the bed lies `depth` below it.
struct RiverPoint {
    Vec3 position{};
    float width = 4.0F;  // bank to bank, in scene units
    float depth = 1.0F;  // water surface to the bed at the middle
    float speed = 1.5F;  // surface speed in units per second; the current of the water
};

// Authoring data of a river: a spline through its points that the water ribbon, the waterfalls and
// the channel cut into a terrain are all made from.
struct RiverComponent {
    static constexpr std::string_view StableId = "kb21.scene.river";
    static constexpr std::uint32_t SchemaVersion = 1U;
    static constexpr std::uint32_t MaxPoints = 32U;

    std::array<RiverPoint, MaxPoints> points{
        RiverPoint{ Vec3{ -8.0F, 0.0F, 0.0F } }, RiverPoint{ Vec3{ 8.0F, 0.0F, 0.0F } }
    };
    std::uint32_t pointCount = 2U;
    // Width of the shoulder that blends the channel into the ground on each side when it is cut into a terrain.
    float bankWidth = 3.0F;
    // A stretch of the river that falls at least this steeply (degrees from horizontal) is a waterfall.
    float waterfallSlopeDegrees = 35.0F;
    // The water material of the stream and of its waterfalls; 0 draws with the engine's error material until one is set.
    std::uint64_t materialAssetId = 0U;
    std::uint64_t waterfallMaterialAssetId = 0U;
    bool enabled = true;
};

[[nodiscard]] inline bool operator==(const RiverPoint& lhs, const RiverPoint& rhs) noexcept {
    return lhs.position.x == rhs.position.x && lhs.position.y == rhs.position.y && lhs.position.z == rhs.position.z &&
        lhs.width == rhs.width && lhs.depth == rhs.depth && lhs.speed == rhs.speed;
}

// Only the points in use count: the unused tail of the array is not part of the river.
[[nodiscard]] inline bool operator==(const RiverComponent& lhs, const RiverComponent& rhs) noexcept {
    if (lhs.pointCount != rhs.pointCount || lhs.bankWidth != rhs.bankWidth || lhs.waterfallSlopeDegrees != rhs.waterfallSlopeDegrees ||
        lhs.materialAssetId != rhs.materialAssetId || lhs.waterfallMaterialAssetId != rhs.waterfallMaterialAssetId || lhs.enabled != rhs.enabled) {
        return false;
    }
    for (std::uint32_t index = 0U; index < lhs.pointCount && index < RiverComponent::MaxPoints; ++index) {
        if (!(lhs.points[index] == rhs.points[index])) return false;
    }
    return true;
}

[[nodiscard]] constexpr bool IsRiverPointCountValid(std::uint32_t count) noexcept {
    return count >= 2U && count <= RiverComponent::MaxPoints;
}

[[nodiscard]] inline bool IsRiverPointValid(const RiverPoint& point) noexcept {
    return std::isfinite(point.position.x) && std::isfinite(point.position.y) && std::isfinite(point.position.z) &&
        std::isfinite(point.width) && point.width > 0.0F && point.width <= 10000.0F &&
        std::isfinite(point.depth) && point.depth > 0.0F && point.depth <= 10000.0F &&
        std::isfinite(point.speed) && point.speed >= 0.0F && point.speed <= 1000.0F;
}

[[nodiscard]] inline bool IsRiverComponentValid(const RiverComponent& river) noexcept {
    if (!IsRiverPointCountValid(river.pointCount) || !std::isfinite(river.bankWidth) || river.bankWidth < 0.0F ||
        river.bankWidth > 10000.0F || !std::isfinite(river.waterfallSlopeDegrees) || river.waterfallSlopeDegrees < 5.0F ||
        river.waterfallSlopeDegrees > 89.0F) {
        return false;
    }
    for (std::uint32_t index = 0U; index < river.pointCount; ++index) {
        if (!IsRiverPointValid(river.points[index])) return false;
    }
    return true;
}

} // namespace kb::scene
