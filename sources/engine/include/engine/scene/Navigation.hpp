#pragma once

#include "engine/math/EngineMath.hpp"

#include <array>
#include <cstdint>
#include <limits>

namespace kb::scene {

// LIB-183: navigation area ids are deliberately bounded to 32 so filters
// remain one machine-word mask in pathfinding hot paths. Area 0 is always
// available and represents ordinary walkable ground.
using NavAreaId = std::uint8_t;
inline constexpr NavAreaId kDefaultNavArea = 0U;
inline constexpr std::uint32_t kNavAreaCount = 32U;
using NavAreaMask = std::uint32_t;
inline constexpr NavAreaMask kAllNavAreas = std::numeric_limits<NavAreaMask>::max();

[[nodiscard]] constexpr bool IsValidNavArea(NavAreaId area) noexcept {
    return area < kNavAreaCount;
}

[[nodiscard]] constexpr NavAreaMask NavAreaBit(NavAreaId area) noexcept {
    return IsValidNavArea(area) ? (NavAreaMask{ 1U } << area) : 0U;
}

// Data-only description of a baked navigation surface. Geometry ownership and
// path requests stay outside this type: LIB-184 supplies the query runtime.
struct NavMesh {
    float agentRadius = 0.5F;
    float agentHeight = 2.0F;
    float agentMaxClimb = 0.4F;
    float agentMaxSlopeDegrees = 45.0F;
    float cellSize = 0.2F;
    float cellHeight = 0.1F;
};

struct NavAgent {
    float radius = 0.5F;
    float height = 2.0F;
    float maxSpeed = 3.5F;
    float acceleration = 8.0F;
    float angularSpeedDegrees = 360.0F;
    float stoppingDistance = 0.1F;
    NavAreaMask areaMask = kAllNavAreas;
    bool enabled = true;
};

enum class NavObstacleShape : std::uint8_t {
    Box,
    Cylinder,
};

struct NavObstacle {
    NavObstacleShape shape = NavObstacleShape::Box;
    kb::math::Vec3 center{};
    kb::math::Vec3 size{ 1.0F, 1.0F, 1.0F };
    float radius = 0.5F;
    float height = 1.0F;
    NavAreaId area = kDefaultNavArea;
    bool carve = true;
    bool enabled = true;
};

// Immutable-by-convention value passed to a navigation query. Excluded areas
// win over included ones, and non-positive/non-finite costs are rejected by
// SetAreaCost rather than silently creating invalid A* edge weights.
class NavQueryFilter final {
public:
    NavQueryFilter() noexcept { areaCosts_.fill(1.0F); }

    [[nodiscard]] NavAreaMask IncludedAreas() const noexcept { return includedAreas_; }
    [[nodiscard]] NavAreaMask ExcludedAreas() const noexcept { return excludedAreas_; }
    void SetIncludedAreas(NavAreaMask areas) noexcept { includedAreas_ = areas; }
    void SetExcludedAreas(NavAreaMask areas) noexcept { excludedAreas_ = areas; }
    [[nodiscard]] bool Allows(NavAreaId area) const noexcept {
        const NavAreaMask bit = NavAreaBit(area);
        return bit != 0U && (includedAreas_ & bit) != 0U && (excludedAreas_ & bit) == 0U;
    }
    [[nodiscard]] float AreaCost(NavAreaId area) const noexcept {
        return IsValidNavArea(area) ? areaCosts_[area] : 0.0F;
    }
    [[nodiscard]] bool SetAreaCost(NavAreaId area, float cost) noexcept {
        if (!IsValidNavArea(area) || !(cost > 0.0F) || cost != cost || cost == std::numeric_limits<float>::infinity()) return false;
        areaCosts_[area] = cost;
        return true;
    }

private:
    NavAreaMask includedAreas_ = kAllNavAreas;
    NavAreaMask excludedAreas_ = 0U;
    std::array<float, kNavAreaCount> areaCosts_{};
};

} // namespace kb::scene
