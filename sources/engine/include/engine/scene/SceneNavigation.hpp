#pragma once

#include "engine/math/DVec3.hpp"
#include "engine/math/EngineMath.hpp"
#include "engine/navigation/NavMeshBuild.hpp"
#include "engine/scene/Navigation.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kb::navigation {
class INavGeometrySource;
struct NavMeshAsset;
}

namespace kb::scene {

class Scene;

// How the crowd moving agents over polygon navigation meshes spends its time. Agents closer than
// `nearDistance` to a level-of-detail focus (SetCrowdFocuses, else every enabled Stream Focus;
// without any focus every agent is near) update every navigation step with full avoidance; the
// others update every `farUpdateInterval` steps with the time that passed, steering and separating
// without sampling velocities.
struct NavCrowdSettings {
    float nearDistance = 60.0F;
    std::uint32_t farUpdateInterval = 4U;
    // Full path searches per step; agents waiting for one stand still and report a Pending path.
    std::uint32_t maxPathSearchesPerStep = 64U;
    // Neighbours an agent avoids and keeps apart from, within this many of its radii.
    float neighbourRangeRadii = 12.0F;
    std::uint32_t maxNeighbours = 6U;
    float separationWeight = 2.0F;
    bool avoidance = true;
};

struct NavCrowdStats {
    std::size_t agents = 0U;
    std::size_t nearUpdates = 0U;
    std::size_t farUpdates = 0U;
    std::size_t pathSearches = 0U;
    std::size_t waitingForPath = 0U;
    std::size_t onLinks = 0U;
    // Wall time of the last navigation step.
    double milliseconds = 0.0;
};

struct NavQueryOptions {
    // Agent profile index of the polygon meshes (see Profiles()).
    std::uint32_t profile = 0U;
    NavAreaMask areas = kAllNavAreas;
    // Half size of the box searched for the polygon nearest to a query point.
    kb::math::Vec3 searchExtents{ 2.0F, 4.0F, 2.0F };
};

struct NavPathResult {
    NavPathStatus status = NavPathStatus::Failed;
    // World positions: the start (on the mesh), every turn and off-mesh link point, the end.
    std::vector<kb::math::DVec3> corners;
    float length = 0.0F;
};

struct NavRaycastResult {
    // False when the start is not on the mesh.
    bool valid = false;
    // True when a wall (or a polygon the areas exclude) stopped the ray before its end.
    bool hit = false;
    float fraction = 1.0F;
    kb::math::DVec3 position{};
    kb::math::Vec3 normal{};
};

// The scene's navigation graph and what its agents are doing on it. While the scene plays, the
// navigation system moves every enabled NavAgent along a path planned on this graph toward the agent's
// destination: carving NavObstacles block the graph nodes and edges they stand on, other obstacles and
// other agents are avoided by choosing velocities that do not collide (reciprocal velocity obstacles).
// The system steps at the scene's fixed step rate in entity id order, so equal input replays exactly.
// An agent with a CharacterController hands its velocity to the physics character; others move their
// Transform. Without a graph agents stay where they are and report a failed path. Destinations, node
// positions and path corners are in the graph's space: world positions minus NavMesh::origin.
//
// When polygon navigation meshes are present (baked tiles, see docs/navigation.md) they take over:
// agents follow path corridors over the polygons as a crowd, carving NavObstacles cut holes into the
// tiles they touch, NavObstacles with an area repaint them, and NavLinks join places the polygons do
// not. The polygons are placed around NavMesh::origin as well.
class SceneNavigation final {
public:
    explicit SceneNavigation(Scene& scene) noexcept;

    // Replaces the graph. Its revision is raised above the previous graph's, so every agent re-plans.
    // The graph's origin also positions the polygon meshes.
    void SetMesh(NavMesh mesh);
    void ClearMesh();
    [[nodiscard]] const NavMesh& Mesh() const noexcept;
    // The closest node an agent with these areas may stand on; nothing for an empty graph.
    [[nodiscard]] std::optional<std::uint32_t> NearestNode(kb::math::Vec3 position, NavAreaMask areas = kAllNavAreas) const;
    // The corners the agent is following (in the graph's space), starting with the next one; empty without a path.
    [[nodiscard]] std::vector<kb::math::Vec3> AgentPath(SceneEntity agent) const;

    // Adds the tiles of a baked navigation mesh; returns a handle for RemoveNavMesh, or 0 with
    // `error` set when its layout differs from the meshes already added. Where two meshes hold the
    // same tile, the one added first is used until it is removed.
    [[nodiscard]] std::uint64_t AddNavMesh(std::shared_ptr<const kb::navigation::NavMeshAsset> asset, std::string* error = nullptr);
    [[nodiscard]] bool RemoveNavMesh(std::uint64_t handle);
    [[nodiscard]] bool HasNavMesh() const noexcept;
    // The agent profiles of the polygon meshes; empty without them.
    [[nodiscard]] std::vector<kb::navigation::NavAgentProfile> Profiles() const;
    // Tiles (one per profile and tile coordinate) currently in use.
    [[nodiscard]] std::size_t NavMeshTileCount(std::uint32_t profile = 0U) const noexcept;
    [[nodiscard]] bool HasNavMeshTile(std::uint32_t profile, kb::navigation::NavTileCoord coord) const noexcept;
    // Tiles rebuilt so far because obstacles, links or geometry changed.
    [[nodiscard]] std::size_t NavMeshTileRebuilds() const noexcept;
    // Rasterises the tiles of every profile overlapping [min, max] on X and Z again, from the
    // scene's static geometry as it is now (colliders, terrain and, with a source that reads them,
    // meshes), and uses them in place of the baked tiles: for geometry built, moved or destroyed
    // while the scene runs. Returns the number of tiles rebuilt.
    std::size_t RebakeTiles(const kb::math::DVec3& min, const kb::math::DVec3& max, kb::navigation::INavGeometrySource* geometry = nullptr);
    // Returns every rebuilt tile to its baked version.
    void RestoreBakedTiles();

    // Queries on the polygon meshes, in world positions. They see the obstacles and links as they
    // are now.
    [[nodiscard]] NavPathResult FindPath(const kb::math::DVec3& start, const kb::math::DVec3& end, const NavQueryOptions& options = {});
    [[nodiscard]] NavRaycastResult Raycast(const kb::math::DVec3& start, const kb::math::DVec3& end, const NavQueryOptions& options = {});
    [[nodiscard]] std::optional<kb::math::DVec3> NearestPoint(const kb::math::DVec3& position, const NavQueryOptions& options = {});
    // Triangles of the polygon mesh of a profile, three world positions each (for debug drawing).
    [[nodiscard]] std::vector<kb::math::DVec3> NavMeshTriangles(std::uint32_t profile = 0U) const;
    // Summed horizontal area of the polygons of a profile, square metres.
    [[nodiscard]] double NavMeshWalkableArea(std::uint32_t profile = 0U) const;

    // Cost multiplier of moving through an area on the polygon meshes (1 by default); ignored when
    // not finite and positive.
    void SetAreaCost(NavAreaId area, float cost) noexcept;
    [[nodiscard]] float AreaCost(NavAreaId area) const noexcept;

    void ConfigureCrowd(const NavCrowdSettings& settings);
    [[nodiscard]] NavCrowdSettings CrowdSettings() const noexcept;
    [[nodiscard]] NavCrowdStats CrowdStats() const noexcept;
    // World positions agents update fully around; replaces the Stream Focus entities as foci.
    void SetCrowdFocuses(std::vector<kb::math::DVec3> focuses);
    // True while the agent is crossing an off-mesh link.
    [[nodiscard]] bool AgentOnLink(SceneEntity agent) const noexcept;

private:
    Scene& scene_;
};

} // namespace kb::scene
