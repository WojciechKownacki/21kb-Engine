#pragma once

#include "engine/math/DVec3.hpp"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace kb::navigation {

// One agent size a navigation mesh is generated for. Every profile has tiles of its own: walls are
// pushed back by the radius, passages lower than the height are closed, ledges up to maxClimb are
// steps and surfaces up to maxSlopeDegrees are walkable.
struct NavAgentProfile {
    static constexpr std::size_t MaxNameBytes = 64U;

    std::string name = "Humanoid";
    float radius = 0.4F;
    float height = 1.8F;
    float maxClimb = 0.4F;
    float maxSlopeDegrees = 45.0F;

    friend bool operator==(const NavAgentProfile&, const NavAgentProfile&) = default;
};

// How scene geometry is turned into navigation tiles. The tile grid is global: tile (x, z) covers
// [x * TileWorldSize(), (x + 1) * TileWorldSize()) on X and the same on Z, so tiles baked in
// separate pieces (world cells) fit together.
struct NavMeshBuildSettings {
    static constexpr std::size_t MaxProfiles = 8U;
    static constexpr std::uint32_t MinTileCells = 16U;
    static constexpr std::uint32_t MaxTileCells = 252U;

    std::vector<NavAgentProfile> profiles{ NavAgentProfile{} };
    // Horizontal and vertical size of the voxels geometry is rasterised into, in metres.
    float cellSize = 0.25F;
    float cellHeight = 0.2F;
    // Side of a tile in voxels. A layer holds at most 255 walkable regions, so very cluttered
    // ground wants smaller tiles.
    std::uint32_t tileCells = 64U;
    // How far simplified polygon edges may stray from the voxel outline, in voxels.
    float edgeMaxError = 1.3F;
    // Which geometry is baked: the meshes of mesh renderers (static meshes and terrain) and the
    // shapes of colliders.
    bool renderMeshes = true;
    bool colliders = true;

    [[nodiscard]] double TileWorldSize() const noexcept {
        return static_cast<double>(cellSize) * static_cast<double>(tileCells);
    }

    friend bool operator==(const NavMeshBuildSettings&, const NavMeshBuildSettings&) = default;
};

// Empty when the settings can be built with, otherwise the first problem.
[[nodiscard]] std::string ValidateNavMeshBuildSettings(const NavMeshBuildSettings& settings);

// True when tiles baked with `a` and with `b` can be used together: the same voxel and tile sizes
// and the same agent profiles in the same order.
[[nodiscard]] bool SameNavMeshLayout(const NavMeshBuildSettings& a, const NavMeshBuildSettings& b) noexcept;

struct NavTileCoord {
    std::int64_t x = 0;
    std::int64_t z = 0;

    friend auto operator<=>(const NavTileCoord&, const NavTileCoord&) = default;
};

// Triangles in world space. Each chunk keeps its vertices as floats relative to a double-precision
// origin, so geometry thousands of kilometres from the world origin keeps its precision.
struct NavGeometryChunk {
    kb::math::DVec3 origin{};
    // x, y, z per vertex, relative to `origin`.
    std::vector<float> vertices;
    // Three vertex indices per triangle.
    std::vector<std::uint32_t> indices;
};

class NavGeometry {
public:
    // Adds a chunk; indices outside the vertex list and non-finite vertices are dropped with
    // their triangles.
    void Add(NavGeometryChunk chunk);
    [[nodiscard]] std::span<const NavGeometryChunk> Chunks() const noexcept { return chunks_; }
    [[nodiscard]] std::size_t TriangleCount() const noexcept;
    [[nodiscard]] bool Empty() const noexcept { return chunks_.empty(); }

private:
    std::vector<NavGeometryChunk> chunks_;
};

// One walkable layer of a tile: a height grid with an area and neighbour connections per column,
// what the tile's polygons are rebuilt from when obstacles change. X and Z are relative to the
// tile's minimum corner and span the whole tile; heights are in voxels above `minY`.
struct NavTileLayer {
    float minY = 0.0F;
    float maxY = 0.0F;
    std::uint16_t heightMin = 0U;
    std::uint16_t heightMax = 0U;
    std::uint8_t minX = 0U;
    std::uint8_t maxX = 0U;
    std::uint8_t minZ = 0U;
    std::uint8_t maxZ = 0U;
    // tileCells * tileCells entries each, row by row along X.
    std::vector<std::uint8_t> heights;
    std::vector<std::uint8_t> areas;
    std::vector<std::uint8_t> connections;

    friend bool operator==(const NavTileLayer&, const NavTileLayer&) = default;
};

struct NavTile {
    std::uint32_t profile = 0U;
    NavTileCoord coord{};
    std::vector<NavTileLayer> layers;

    friend bool operator==(const NavTile&, const NavTile&) = default;
};

// Area code stored in a layer for ordinary walkable ground; area id `a` is stored as `a + 1` and 0
// is not walkable.
inline constexpr std::uint8_t kNavLayerGroundArea = 1U;

struct NavMeshBakeStats {
    std::size_t tiles = 0U;
    std::size_t layers = 0U;
    std::size_t sourceTriangles = 0U;
    double milliseconds = 0.0;
};

struct NavMeshBakeResult {
    bool succeeded = false;
    // Tiles with at least one walkable layer, sorted by profile, then tile x, then tile z.
    std::vector<NavTile> tiles;
    NavMeshBakeStats stats;
    std::string error;
};

class NavMeshBuilder {
public:
    NavMeshBuilder() = delete;

    // Rasterises the geometry touching the tile (and the border around it the agent radius needs)
    // and returns the walkable layers of the tile; an empty layer list when nothing is walkable.
    [[nodiscard]] static bool BuildTile(const NavMeshBuildSettings& settings, std::uint32_t profile, NavTileCoord coord,
        const NavGeometry& geometry, NavTile& tile, std::string& error);

    // Every tile of every profile the geometry touches. Tiles are built on up to `workerThreads`
    // threads (0 picks one per core, at most 8); the result does not depend on the thread count.
    [[nodiscard]] static NavMeshBakeResult Bake(const NavMeshBuildSettings& settings, const NavGeometry& geometry, std::uint32_t workerThreads = 0U);
};

} // namespace kb::navigation
