#pragma once

#include "engine/math/DVec3.hpp"
#include "engine/navigation/NavMeshAsset.hpp"
#include "engine/scene/Navigation.hpp"

#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace kb::navigation {

// A NavObstacle placed in the navigation mesh's space (world minus the origin).
struct NavRuntimeObstacle {
    std::uint64_t id = 0U;
    kb::scene::NavObstacleShape shape = kb::scene::NavObstacleShape::Box;
    kb::math::Vec3 center{};
    // Rotation about the vertical axis, radians (boxes).
    float yaw = 0.0F;
    kb::math::Vec3 halfExtents{ 0.5F, 0.5F, 0.5F };
    float radius = 0.5F;
    float height = 1.0F;
    bool carve = true;
    kb::scene::NavAreaId area = kb::scene::kDefaultNavArea;
};

[[nodiscard]] inline bool SameVec3(kb::math::Vec3 a, kb::math::Vec3 b) noexcept {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

[[nodiscard]] inline bool operator==(const NavRuntimeObstacle& a, const NavRuntimeObstacle& b) noexcept {
    return a.id == b.id && a.shape == b.shape && SameVec3(a.center, b.center) && a.yaw == b.yaw && SameVec3(a.halfExtents, b.halfExtents) &&
        a.radius == b.radius && a.height == b.height && a.carve == b.carve && a.area == b.area;
}

// A NavLink placed in the navigation mesh's space.
struct NavRuntimeLink {
    std::uint64_t id = 0U;
    kb::math::Vec3 start{};
    kb::math::Vec3 end{};
    float radius = 0.5F;
    kb::scene::NavLinkKind kind = kb::scene::NavLinkKind::Jump;
    kb::scene::NavAreaId area = kb::scene::kDefaultNavArea;
    bool bidirectional = true;
};

[[nodiscard]] inline bool operator==(const NavRuntimeLink& a, const NavRuntimeLink& b) noexcept {
    return a.id == b.id && SameVec3(a.start, b.start) && SameVec3(a.end, b.end) && a.radius == b.radius && a.kind == b.kind && a.area == b.area &&
        a.bidirectional == b.bidirectional;
}

// Filters polygons by the engine's 32 navigation areas (Detour area = area id + 1) and applies the
// scene's area costs. Used for queries; crowds filter by polygon flags (NavMeshRuntime::FilterSlot).
class NavAreaFilter final : public dtQueryFilter {
public:
    NavAreaFilter() noexcept;
    void SetAreas(kb::scene::NavAreaMask areas) noexcept { areas_ = areas; }
    [[nodiscard]] kb::scene::NavAreaMask Areas() const noexcept { return areas_; }
    bool passFilter(const dtPolyRef ref, const dtMeshTile* tile, const dtPoly* poly) const override;

private:
    kb::scene::NavAreaMask areas_ = kb::scene::kAllNavAreas;
};

struct NavStraightPath {
    kb::scene::NavPathStatus status = kb::scene::NavPathStatus::Failed;
    // Corners in the mesh's space, start first, the (reached) end last.
    std::vector<kb::math::Vec3> corners;
    // True for a corner where an off-mesh link begins.
    std::vector<bool> linkStarts;
};

struct NavRaycast {
    bool valid = false;
    bool hit = false;
    float fraction = 1.0F;
    kb::math::Vec3 position{};
    kb::math::Vec3 normal{};
};

// The polygon navigation meshes of a scene: one Detour mesh per agent profile, filled from baked
// tile sets (a scene's asset, the cells of a streamed world) and rebuilt tile by tile when obstacles
// or off-mesh links change. Everything is in the space of `origin`: world positions minus it, so a
// mesh far from the world origin keeps float precision around that origin.
class NavMeshRuntime {
public:
    NavMeshRuntime();
    ~NavMeshRuntime();
    NavMeshRuntime(const NavMeshRuntime&) = delete;
    NavMeshRuntime& operator=(const NavMeshRuntime&) = delete;

    // 0 with `error` set when the asset's layout differs from the tile sets already present.
    [[nodiscard]] std::uint64_t AddTileSet(std::shared_ptr<const NavMeshAsset> asset, std::string& error);
    [[nodiscard]] bool RemoveTileSet(std::uint64_t handle);
    [[nodiscard]] bool Empty() const noexcept { return sets_.empty(); }
    [[nodiscard]] std::size_t TileSetCount() const noexcept { return sets_.size(); }
    [[nodiscard]] const NavMeshBuildSettings* Layout() const noexcept { return layout_ ? &*layout_ : nullptr; }
    [[nodiscard]] std::uint32_t ProfileCount() const noexcept;
    // The smallest profile at least as wide as the agent, else the widest.
    [[nodiscard]] std::uint32_t ProfileForRadius(float radius) const noexcept;

    void SetOrigin(const kb::math::DVec3& origin);
    [[nodiscard]] const kb::math::DVec3& Origin() const noexcept { return origin_; }

    // Replace the overlays; the tiles any changed obstacle or link touches (before or after) are
    // rebuilt. Lists are sorted by id.
    void SetObstacles(std::vector<NavRuntimeObstacle> obstacles);
    void SetLinks(std::vector<NavRuntimeLink> links);
    [[nodiscard]] const std::vector<NavRuntimeObstacle>& Obstacles() const noexcept { return obstacles_; }

    // Uses `tile`, rebuilt from the scene's current geometry, in place of the baked tile at its
    // coordinate (a tile without layers leaves the coordinate empty). Needs a layout.
    [[nodiscard]] bool UseRebuiltTile(NavTile tile);
    // Returns every coordinate to its baked tile; the number of rebuilt tiles dropped.
    std::size_t DropRebuiltTiles();

    // Raised whenever a polygon tile is added, removed or rebuilt.
    [[nodiscard]] std::uint64_t Revision() const noexcept { return revision_; }
    [[nodiscard]] std::size_t TileRebuilds() const noexcept { return rebuilds_; }
    // Baked tiles in use (one per profile and coordinate) and Detour tiles (one per layer).
    [[nodiscard]] std::size_t ActiveTileCount(std::uint32_t profile) const noexcept;
    [[nodiscard]] std::size_t PolygonTileCount(std::uint32_t profile) const noexcept;
    [[nodiscard]] bool HasTile(std::uint32_t profile, NavTileCoord coord) const noexcept;

    [[nodiscard]] dtNavMesh* Mesh(std::uint32_t profile) noexcept;
    [[nodiscard]] const dtNavMesh* Mesh(std::uint32_t profile) const noexcept;
    [[nodiscard]] dtNavMeshQuery* Query(std::uint32_t profile) noexcept;
    // The link an off-mesh connection polygon stands for.
    [[nodiscard]] const NavRuntimeLink* LinkForPolygon(std::uint32_t profile, dtPolyRef polygon) const;

    void SetAreaCost(kb::scene::NavAreaId area, float cost) noexcept;
    [[nodiscard]] float AreaCost(kb::scene::NavAreaId area) const noexcept;
    void ConfigureFilter(NavAreaFilter& filter, kb::scene::NavAreaMask areas) const noexcept;

    // A crowd filters polygons by their 16 flag bits only, so every area mask its members use gets a
    // slot: polygon flag bit `slot` is set on the polygons whose area is in the slot's mask. Slot 0
    // always holds every area. Returns -1 when all slots hold other masks.
    static constexpr int kCrowdFilterCount = 16;
    [[nodiscard]] int FilterSlot(kb::scene::NavAreaMask areas);
    // Frees the slots of masks no longer in use (`used` sorted).
    void KeepFilterSlots(std::span<const kb::scene::NavAreaMask> used);
    // Admits the polygons of a slot, with the scene's area costs.
    void ConfigureCrowdFilter(dtQueryFilter& filter, int slot) const noexcept;
    // Raised whenever the Detour mesh of a profile is created anew (a new origin, more tile slots);
    // a crowd walking the old one must start over.
    [[nodiscard]] std::uint64_t MeshGeneration(std::uint32_t profile) const noexcept;

    // Queries in the mesh's space. `extents` is the half size of the box searched for the nearest
    // polygon around a point.
    [[nodiscard]] std::optional<std::pair<dtPolyRef, kb::math::Vec3>> Nearest(std::uint32_t profile, kb::math::Vec3 position,
        kb::math::Vec3 extents, const NavAreaFilter& filter);
    [[nodiscard]] NavStraightPath FindPath(std::uint32_t profile, kb::math::Vec3 start, kb::math::Vec3 end, kb::math::Vec3 extents,
        const NavAreaFilter& filter);
    [[nodiscard]] NavRaycast Raycast(std::uint32_t profile, kb::math::Vec3 start, kb::math::Vec3 end, kb::math::Vec3 extents,
        const NavAreaFilter& filter);
    // Triangles of every polygon of a profile, three corners each, in the mesh's space.
    [[nodiscard]] std::vector<kb::math::Vec3> DebugTriangles(std::uint32_t profile) const;
    // Summed horizontal area of the polygons of a profile.
    [[nodiscard]] double WalkableArea(std::uint32_t profile) const;

private:
    struct TileKey {
        std::uint32_t profile = 0U;
        NavTileCoord coord{};
        friend auto operator<=>(const TileKey&, const TileKey&) = default;
    };
    struct TileOwner {
        std::uint64_t handle = 0U;
        std::size_t index = 0U;
    };
    struct TileSlot {
        std::vector<TileOwner> owners;
        // A tile rebuilt from the scene's current geometry, used instead of the owners' baked one.
        std::optional<NavTile> rebuilt;
        std::vector<dtTileRef> placed;
        [[nodiscard]] bool Used() const noexcept { return rebuilt.has_value() || !owners.empty(); }
    };
    struct MeshDeleter {
        void operator()(dtNavMesh* mesh) const noexcept;
    };
    struct QueryDeleter {
        void operator()(dtNavMeshQuery* query) const noexcept;
    };
    struct ProfileMesh {
        std::unique_ptr<dtNavMesh, MeshDeleter> mesh;
        std::unique_ptr<dtNavMeshQuery, QueryDeleter> query;
        int capacity = 0;
        std::uint64_t generation = 0U;
    };

    [[nodiscard]] bool CreateMesh(std::uint32_t profile, int capacity);
    [[nodiscard]] unsigned short PolygonFlags(unsigned char areaCode) const noexcept;
    void RefreshPolygonFlags();
    void PlaceAll(std::uint32_t profile);
    void Place(const TileKey& key, TileSlot& slot);
    void Unplace(const TileKey& key, TileSlot& slot);
    [[nodiscard]] bool BuildLayer(const TileKey& key, const NavTile& tile, std::size_t layer, unsigned char*& data, int& size) const;
    [[nodiscard]] NavTileCoord TileOf(double x, double z) const noexcept;
    void MarkTiles(kb::math::Vec3 low, kb::math::Vec3 high, std::set<TileKey>& dirty) const;
    void Rebuild(const std::set<TileKey>& dirty);
    void Reset();

    std::map<std::uint64_t, std::shared_ptr<const NavMeshAsset>> sets_;
    std::map<TileKey, TileSlot> tiles_;
    std::optional<NavMeshBuildSettings> layout_;
    std::vector<ProfileMesh> meshes_;
    double tileSize_ = 1.0;
    kb::math::DVec3 origin_{};
    NavTileCoord originTile_{};
    std::vector<NavRuntimeObstacle> obstacles_;
    std::map<std::uint32_t, NavRuntimeLink> links_;
    std::map<std::uint64_t, std::uint32_t> linkUserIds_;
    std::uint32_t nextLinkUserId_ = 1U;
    std::array<float, kb::scene::kNavAreaCount> costs_{};
    // Area mask per crowd filter slot; 0 marks a free slot.
    std::array<kb::scene::NavAreaMask, kCrowdFilterCount> filterMasks_{ kb::scene::kAllNavAreas };
    std::uint64_t nextGeneration_ = 1U;
    std::uint64_t nextHandle_ = 1U;
    std::uint64_t revision_ = 1U;
    std::size_t rebuilds_ = 0U;
};

} // namespace kb::navigation
