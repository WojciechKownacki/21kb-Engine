#pragma once

#include "engine/math/EngineMath.hpp"

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

// The state of an agent's path on the navigation mesh. Cancelled is not produced any more; it stays
// so agents saved with it still load.
enum class NavPathStatus : std::uint8_t { Invalid, Pending, Complete, Partial, Failed, Cancelled };

// An agent the navigation system moves over the scene's navigation meshes (SceneNavigation). The
// destination is in the navigation space: world position minus SceneNavigation::Origin(). Velocity,
// remaining distance and path status are written back every navigation step.
struct NavAgent {
    float radius = 0.5F;
    float height = 2.0F;
    float maxSpeed = 3.5F;
    float acceleration = 8.0F;
    float angularSpeedDegrees = 360.0F;
    float stoppingDistance = 0.1F;
    NavAreaMask areaMask = kAllNavAreas;
    kb::math::Vec3 destination{};
    kb::math::Vec3 velocity{};
    float remainingDistance = 0.0F;
    NavPathStatus pathStatus = NavPathStatus::Invalid;
    bool enabled = true;
};

enum class NavObstacleShape : std::uint8_t {
    Box,
    Cylinder,
};

// A box or cylinder in the owner's space. A carving obstacle cuts a hole into the navigation mesh; a
// non-carving one with an area other than 0 repaints the polygons under it; any other is steered
// around by agents.
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

// How an agent crosses an off-mesh link: a jump arcs from start to end, a ladder climbs vertically
// before (going up) or after (going down) stepping across, a walk moves straight between the points.
enum class NavLinkKind : std::uint8_t {
    Jump,
    Ladder,
    Walk,
};

// An off-mesh link: a connection between two points the navigation mesh does not join (a jump down
// a ledge, a ladder, a gap). Start and end are offsets in the owner's space; each joins the mesh
// within `radius` of it.
struct NavLink {
    kb::math::Vec3 start{};
    kb::math::Vec3 end{ 0.0F, 0.0F, 2.0F };
    float radius = 0.5F;
    NavLinkKind kind = NavLinkKind::Jump;
    NavAreaId area = kDefaultNavArea;
    bool bidirectional = true;
    bool enabled = true;
};

} // namespace kb::scene
