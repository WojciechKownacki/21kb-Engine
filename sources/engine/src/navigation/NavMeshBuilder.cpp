#include "engine/navigation/NavMeshBuild.hpp"

#include "engine/ecs/WorkerPool.hpp"
#include "engine/navigation/NavMeshAsset.hpp"

#include <Recast.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <unordered_map>
#include <utility>

namespace kb::navigation {
namespace {

constexpr double kPi = 3.14159265358979323846;

struct TriangleRef {
    std::uint32_t chunk = 0U;
    std::uint32_t triangle = 0U;
};

struct HeightfieldDeleter {
    void operator()(rcHeightfield* value) const noexcept { rcFreeHeightField(value); }
};
struct CompactHeightfieldDeleter {
    void operator()(rcCompactHeightfield* value) const noexcept { rcFreeCompactHeightfield(value); }
};
struct LayerSetDeleter {
    void operator()(rcHeightfieldLayerSet* value) const noexcept { rcFreeHeightfieldLayerSet(value); }
};

[[nodiscard]] bool Finite(float value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] bool InRange(float value, float low, float high) noexcept {
    return std::isfinite(value) && value >= low && value <= high;
}

// Voxel counts the Recast passes take for a profile.
struct ProfileVoxels {
    int height = 0;
    int climb = 0;
    int radius = 0;
    int border = 0;
};

[[nodiscard]] ProfileVoxels Voxels(const NavMeshBuildSettings& settings, const NavAgentProfile& profile) noexcept {
    ProfileVoxels voxels;
    voxels.height = static_cast<int>(std::ceil(profile.height / settings.cellHeight));
    voxels.climb = static_cast<int>(std::floor(profile.maxClimb / settings.cellHeight));
    voxels.radius = static_cast<int>(std::ceil(profile.radius / settings.cellSize));
    voxels.border = voxels.radius + 3;
    return voxels;
}

// Horizontal reach of the geometry a tile needs beyond its own square: the border its agents'
// erosion looks into.
[[nodiscard]] double BorderWorld(const NavMeshBuildSettings& settings) noexcept {
    int border = 0;
    for (const NavAgentProfile& profile : settings.profiles) {
        border = std::max(border, Voxels(settings, profile).border);
    }
    return static_cast<double>(border + 1) * static_cast<double>(settings.cellSize);
}

[[nodiscard]] std::int64_t TileIndex(double value, double tileSize) noexcept {
    return static_cast<std::int64_t>(std::floor(value / tileSize));
}

// Range of tiles a triangle (with the border around tiles) touches; false for a triangle so far out
// its tile index does not fit.
[[nodiscard]] bool TriangleTiles(const NavGeometryChunk& chunk, std::uint32_t triangle, double tileSize, double border,
    NavTileCoord& low, NavTileCoord& high) noexcept {
    double minX = std::numeric_limits<double>::infinity();
    double minZ = std::numeric_limits<double>::infinity();
    double maxX = -std::numeric_limits<double>::infinity();
    double maxZ = -std::numeric_limits<double>::infinity();
    for (std::uint32_t corner = 0U; corner < 3U; ++corner) {
        const std::uint32_t vertex = chunk.indices[triangle * 3U + corner];
        const double x = chunk.origin.x + static_cast<double>(chunk.vertices[vertex * 3U]);
        const double z = chunk.origin.z + static_cast<double>(chunk.vertices[vertex * 3U + 2U]);
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
        minZ = std::min(minZ, z);
        maxZ = std::max(maxZ, z);
    }
    constexpr double kLimit = 9.0e15;
    if (!(std::fabs(minX) < kLimit && std::fabs(maxX) < kLimit && std::fabs(minZ) < kLimit && std::fabs(maxZ) < kLimit)) {
        return false;
    }
    low = { TileIndex(minX - border, tileSize), TileIndex(minZ - border, tileSize) };
    high = { TileIndex(maxX + border, tileSize), TileIndex(maxZ + border, tileSize) };
    return true;
}

[[nodiscard]] bool BuildTileFromTriangles(const NavMeshBuildSettings& settings, std::uint32_t profileIndex, NavTileCoord coord,
    const NavGeometry& geometry, std::span<const TriangleRef> triangles, NavTile& tile, std::string& error) {
    tile = NavTile{ .profile = profileIndex, .coord = coord, .layers = {} };
    if (triangles.empty()) {
        return true;
    }
    const NavAgentProfile& profile = settings.profiles[profileIndex];
    const ProfileVoxels voxels = Voxels(settings, profile);
    const float cellSize = settings.cellSize;
    const float cellHeight = settings.cellHeight;
    const int tileCells = static_cast<int>(settings.tileCells);
    const double tileSize = settings.TileWorldSize();
    const double tileMinX = static_cast<double>(coord.x) * tileSize;
    const double tileMinZ = static_cast<double>(coord.z) * tileSize;

    // Tile-local float triangles: X and Z from the tile's minimum corner, Y as in the world.
    std::vector<float> vertices;
    vertices.reserve(triangles.size() * 9U);
    std::vector<unsigned char> areas;
    areas.reserve(triangles.size());
    float minY = std::numeric_limits<float>::infinity();
    float maxY = -std::numeric_limits<float>::infinity();
    const double walkableNormalY = std::cos(static_cast<double>(profile.maxSlopeDegrees) * kPi / 180.0);
    const std::span<const NavGeometryChunk> chunks = geometry.Chunks();
    for (const TriangleRef& reference : triangles) {
        const NavGeometryChunk& chunk = chunks[reference.chunk];
        std::array<double, 9U> corner{};
        for (std::uint32_t index = 0U; index < 3U; ++index) {
            const std::uint32_t vertex = chunk.indices[reference.triangle * 3U + index];
            corner[index * 3U + 0U] = (chunk.origin.x - tileMinX) + static_cast<double>(chunk.vertices[vertex * 3U + 0U]);
            corner[index * 3U + 1U] = chunk.origin.y + static_cast<double>(chunk.vertices[vertex * 3U + 1U]);
            corner[index * 3U + 2U] = (chunk.origin.z - tileMinZ) + static_cast<double>(chunk.vertices[vertex * 3U + 2U]);
        }
        // Walkable when the surface faces up or down within the slope limit: single-sided
        // geometry authored with either winding is walked on from above.
        const double e0x = corner[3] - corner[0], e0y = corner[4] - corner[1], e0z = corner[5] - corner[2];
        const double e1x = corner[6] - corner[0], e1y = corner[7] - corner[1], e1z = corner[8] - corner[2];
        const double nx = e0y * e1z - e0z * e1y;
        const double ny = e0z * e1x - e0x * e1z;
        const double nz = e0x * e1y - e0y * e1x;
        const double length = std::sqrt(nx * nx + ny * ny + nz * nz);
        const bool walkable = length > 0.0 && std::fabs(ny) / length >= walkableNormalY;
        const bool flip = ny < 0.0;
        areas.push_back(walkable ? kNavLayerGroundArea : RC_NULL_AREA);
        for (std::uint32_t index = 0U; index < 3U; ++index) {
            // Recast rasterises either winding; keep the upward one so its slope test agrees.
            const std::uint32_t source = flip ? (index == 0U ? 0U : 3U - index) : index;
            const float x = static_cast<float>(corner[source * 3U + 0U]);
            const float y = static_cast<float>(corner[source * 3U + 1U]);
            const float z = static_cast<float>(corner[source * 3U + 2U]);
            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
        }
    }
    if (!Finite(minY) || !Finite(maxY)) {
        return true;
    }

    const float border = static_cast<float>(voxels.border) * cellSize;
    const float tileExtent = static_cast<float>(tileCells) * cellSize;
    const float bmin[3] = { -border, minY - cellHeight, -border };
    const float bmax[3] = { tileExtent + border, maxY + cellHeight, tileExtent + border };
    const int size = tileCells + voxels.border * 2;

    rcContext context{ false };
    std::unique_ptr<rcHeightfield, HeightfieldDeleter> heightfield{ rcAllocHeightfield() };
    if (!heightfield || !rcCreateHeightfield(&context, *heightfield, size, size, bmin, bmax, cellSize, cellHeight)) {
        error = "out of memory creating the heightfield of tile " + std::to_string(coord.x) + "," + std::to_string(coord.z);
        return false;
    }
    if (!rcRasterizeTriangles(&context, vertices.data(), areas.data(), static_cast<int>(areas.size()), *heightfield, voxels.climb)) {
        error = "could not rasterise the geometry of tile " + std::to_string(coord.x) + "," + std::to_string(coord.z);
        return false;
    }
    rcFilterLowHangingWalkableObstacles(&context, voxels.climb, *heightfield);
    rcFilterLedgeSpans(&context, voxels.height, voxels.climb, *heightfield);
    rcFilterWalkableLowHeightSpans(&context, voxels.height, *heightfield);

    std::unique_ptr<rcCompactHeightfield, CompactHeightfieldDeleter> compact{ rcAllocCompactHeightfield() };
    if (!compact || !rcBuildCompactHeightfield(&context, voxels.height, voxels.climb, *heightfield, *compact)) {
        error = "could not build the compact heightfield of tile " + std::to_string(coord.x) + "," + std::to_string(coord.z);
        return false;
    }
    heightfield.reset();
    if (!rcErodeWalkableArea(&context, voxels.radius, *compact)) {
        error = "could not erode the walkable area of tile " + std::to_string(coord.x) + "," + std::to_string(coord.z);
        return false;
    }
    // Walkable ground outside every bake box is dropped, so the mesh ends at the boxes' sides.
    if (const std::span<const NavBakeBox> boxes = geometry.BakeBoxes(); !boxes.empty()) {
        for (int z = 0; z < compact->height; ++z) {
            for (int x = 0; x < compact->width; ++x) {
                const double worldX = tileMinX + static_cast<double>(bmin[0]) + (static_cast<double>(x) + 0.5) * cellSize;
                const double worldZ = tileMinZ + static_cast<double>(bmin[2]) + (static_cast<double>(z) + 0.5) * cellSize;
                const rcCompactCell& cell = compact->cells[x + z * compact->width];
                for (unsigned int span = cell.index; span < cell.index + cell.count; ++span) {
                    const double worldY = static_cast<double>(bmin[1]) + static_cast<double>(compact->spans[span].y) * cellHeight;
                    const bool inside = std::ranges::any_of(boxes, [&](const NavBakeBox& box) {
                        return worldX >= box.min.x && worldX <= box.max.x && worldZ >= box.min.z && worldZ <= box.max.z &&
                            worldY >= box.min.y - cellHeight && worldY <= box.max.y;
                    });
                    if (!inside) compact->areas[span] = RC_NULL_AREA;
                }
            }
        }
    }
    std::unique_ptr<rcHeightfieldLayerSet, LayerSetDeleter> layers{ rcAllocHeightfieldLayerSet() };
    if (!layers || !rcBuildHeightfieldLayers(&context, *compact, voxels.border, voxels.height, *layers)) {
        error = "could not build the walkable layers of tile " + std::to_string(coord.x) + "," + std::to_string(coord.z);
        return false;
    }
    const std::size_t cells = static_cast<std::size_t>(tileCells) * static_cast<std::size_t>(tileCells);
    for (int index = 0; index < layers->nlayers && tile.layers.size() < 32U; ++index) {
        const rcHeightfieldLayer& source = layers->layers[index];
        if (source.width != tileCells || source.height != tileCells) {
            error = "tile " + std::to_string(coord.x) + "," + std::to_string(coord.z) + " produced a layer of an unexpected size";
            return false;
        }
        NavTileLayer layer;
        layer.minY = source.bmin[1];
        layer.maxY = source.bmax[1];
        layer.heightMin = static_cast<std::uint16_t>(std::clamp(source.hmin, 0, 0xFFFF));
        layer.heightMax = static_cast<std::uint16_t>(std::clamp(source.hmax, 0, 0xFFFF));
        layer.minX = static_cast<std::uint8_t>(source.minx);
        layer.maxX = static_cast<std::uint8_t>(source.maxx);
        layer.minZ = static_cast<std::uint8_t>(source.miny);
        layer.maxZ = static_cast<std::uint8_t>(source.maxy);
        layer.heights.assign(source.heights, source.heights + cells);
        layer.areas.assign(source.areas, source.areas + cells);
        layer.connections.assign(source.cons, source.cons + cells);
        tile.layers.push_back(std::move(layer));
    }
    return true;
}

// Whether the bake builds the tile at all: always without bake boxes, else when the tile touches one.
[[nodiscard]] bool TileInBakeBoxes(const NavGeometry& geometry, NavTileCoord coord, double tileSize) {
    const std::span<const NavBakeBox> boxes = geometry.BakeBoxes();
    const double minX = static_cast<double>(coord.x) * tileSize;
    const double minZ = static_cast<double>(coord.z) * tileSize;
    return boxes.empty() || std::ranges::any_of(boxes, [&](const NavBakeBox& box) {
        return box.max.x >= minX && box.min.x <= minX + tileSize && box.max.z >= minZ && box.min.z <= minZ + tileSize;
    });
}

[[nodiscard]] std::map<NavTileCoord, std::vector<TriangleRef>> BucketTriangles(const NavMeshBuildSettings& settings,
    const NavGeometry& geometry, std::size_t maxTiles, std::string& error) {
    std::map<NavTileCoord, std::vector<TriangleRef>> buckets;
    const double tileSize = settings.TileWorldSize();
    const double border = BorderWorld(settings);
    const std::span<const NavGeometryChunk> chunks = geometry.Chunks();
    for (std::uint32_t chunkIndex = 0U; chunkIndex < chunks.size(); ++chunkIndex) {
        const NavGeometryChunk& chunk = chunks[chunkIndex];
        const auto triangleCount = static_cast<std::uint32_t>(chunk.indices.size() / 3U);
        for (std::uint32_t triangle = 0U; triangle < triangleCount; ++triangle) {
            NavTileCoord low{};
            NavTileCoord high{};
            if (!TriangleTiles(chunk, triangle, tileSize, border, low, high)) {
                continue;
            }
            if (static_cast<double>(high.x - low.x + 1) * static_cast<double>(high.z - low.z + 1) > static_cast<double>(maxTiles)) {
                error = "one triangle of the geometry spans more navigation tiles than a navigation mesh may hold";
                return {};
            }
            for (std::int64_t z = low.z; z <= high.z; ++z) {
                for (std::int64_t x = low.x; x <= high.x; ++x) {
                    std::vector<TriangleRef>& bucket = buckets[NavTileCoord{ x, z }];
                    bucket.push_back({ chunkIndex, triangle });
                }
            }
            if (buckets.size() > maxTiles) {
                error = "the geometry spans more navigation tiles than a navigation mesh may hold";
                return {};
            }
        }
    }
    std::erase_if(buckets, [&](const auto& entry) { return !TileInBakeBoxes(geometry, entry.first, tileSize); });
    return buckets;
}

// Runs build(job) for every job, in parallel on `workers` when it is running, else on a pool started for the call.
void RunTileJobs(std::size_t jobs, kb::ecs::WorkerPool* workers, const std::function<void(std::size_t)>& build) {
    if (jobs == 1U) {
        build(0U);
    } else if (jobs > 1U) {
        // Without a running pool from the caller the bake starts the engine's own for its duration.
        std::optional<kb::ecs::WorkerPool> local;
        if (workers == nullptr || !workers->Running()) {
            local.emplace(kb::ecs::WorkerPoolConfig{ .workerCount = NavMeshBuilder::DefaultBakeWorkerCount() });
            workers = &*local;
        }
        // One tile per chunk: tiles differ widely in cost, so the pool balances them one by one.
        workers->ParallelForChunks(jobs, 1U, [&build](kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk& chunk) {
            for (std::size_t job = chunk.begin; job < chunk.begin + chunk.count; ++job) build(job);
        });
    }
}

[[nodiscard]] std::uint64_t ChunkHash(const NavGeometryChunk& chunk) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    const auto mix = [&hash](const void* data, std::size_t bytes) {
        const auto* byte = static_cast<const unsigned char*>(data);
        for (std::size_t index = 0U; index < bytes; ++index) {
            hash = (hash ^ byte[index]) * 1099511628211ULL;
        }
    };
    const std::array<double, 3> origin{ chunk.origin.x, chunk.origin.y, chunk.origin.z };
    mix(origin.data(), sizeof(origin));
    mix(chunk.vertices.data(), chunk.vertices.size() * sizeof(float));
    mix(chunk.indices.data(), chunk.indices.size() * sizeof(std::uint32_t));
    return hash;
}

// The chunks of `from` with no equal chunk in `other`; each chunk of `other` matches one chunk at most.
[[nodiscard]] std::vector<const NavGeometryChunk*> UnmatchedChunks(std::span<const NavGeometryChunk> from, std::span<const NavGeometryChunk> other) {
    std::unordered_multimap<std::uint64_t, std::size_t> index;
    for (std::size_t chunk = 0U; chunk < other.size(); ++chunk) index.emplace(ChunkHash(other[chunk]), chunk);
    std::vector<bool> used(other.size(), false);
    std::vector<const NavGeometryChunk*> unmatched;
    for (const NavGeometryChunk& chunk : from) {
        const auto [first, last] = index.equal_range(ChunkHash(chunk));
        const auto match = std::find_if(first, last, [&](const auto& entry) {
            const NavGeometryChunk& candidate = other[entry.second];
            return !used[entry.second] && candidate.origin.x == chunk.origin.x && candidate.origin.y == chunk.origin.y &&
                candidate.origin.z == chunk.origin.z && candidate.vertices == chunk.vertices && candidate.indices == chunk.indices;
        });
        if (match == last) {
            unmatched.push_back(&chunk);
        } else {
            used[match->second] = true;
        }
    }
    return unmatched;
}

[[nodiscard]] std::vector<const NavBakeBox*> UnmatchedBoxes(std::span<const NavBakeBox> from, std::span<const NavBakeBox> other) {
    std::vector<bool> used(other.size(), false);
    std::vector<const NavBakeBox*> unmatched;
    for (const NavBakeBox& box : from) {
        std::size_t match = 0U;
        while (match < other.size() && (used[match] || other[match].min.x != box.min.x || other[match].min.y != box.min.y ||
            other[match].min.z != box.min.z || other[match].max.x != box.max.x || other[match].max.y != box.max.y || other[match].max.z != box.max.z)) {
            ++match;
        }
        if (match == other.size()) {
            unmatched.push_back(&box);
        } else {
            used[match] = true;
        }
    }
    return unmatched;
}

} // namespace

std::optional<std::vector<NavTileCoord>> ChangedNavTiles(const NavMeshBuildSettings& settings, const NavGeometry& before, const NavGeometry& after) {
    if (before.BakeBoxes().empty() != after.BakeBoxes().empty() || !ValidateNavMeshBuildSettings(settings).empty()) {
        return std::nullopt;
    }
    const double tileSize = settings.TileWorldSize();
    const double border = BorderWorld(settings);
    const std::size_t maxTiles = NavMeshAsset::MaxTiles;
    std::set<NavTileCoord> tiles;
    bool everything = false;
    const auto addRange = [&](NavTileCoord low, NavTileCoord high) {
        if (static_cast<double>(high.x - low.x + 1) * static_cast<double>(high.z - low.z + 1) > static_cast<double>(maxTiles)) {
            everything = true;
            return;
        }
        for (std::int64_t z = low.z; z <= high.z && !everything; ++z) {
            for (std::int64_t x = low.x; x <= high.x; ++x) tiles.insert(NavTileCoord{ x, z });
        }
        everything = everything || tiles.size() > maxTiles;
    };
    const auto addChanges = [&](const NavGeometry& from, const NavGeometry& other) {
        for (const NavGeometryChunk* chunk : UnmatchedChunks(from.Chunks(), other.Chunks())) {
            const auto triangleCount = static_cast<std::uint32_t>(chunk->indices.size() / 3U);
            for (std::uint32_t triangle = 0U; triangle < triangleCount && !everything; ++triangle) {
                NavTileCoord low{};
                NavTileCoord high{};
                if (TriangleTiles(*chunk, triangle, tileSize, border, low, high)) addRange(low, high);
            }
        }
        // A box moves the clipping of the cells around it, and the erosion of the tiles near them.
        for (const NavBakeBox* box : UnmatchedBoxes(from.BakeBoxes(), other.BakeBoxes())) {
            if (!everything) {
                addRange({ TileIndex(box->min.x - border, tileSize), TileIndex(box->min.z - border, tileSize) },
                    { TileIndex(box->max.x + border, tileSize), TileIndex(box->max.z + border, tileSize) });
            }
        }
    };
    addChanges(before, after);
    addChanges(after, before);
    if (everything) {
        return std::nullopt;
    }
    return std::vector<NavTileCoord>(tiles.begin(), tiles.end());
}

std::string ValidateNavMeshBuildSettings(const NavMeshBuildSettings& settings) {
    if (!InRange(settings.cellSize, 0.01F, 10.0F)) return "navigation cell size must be between 0.01 and 10 metres";
    if (!InRange(settings.cellHeight, 0.01F, 10.0F)) return "navigation cell height must be between 0.01 and 10 metres";
    if (settings.tileCells < NavMeshBuildSettings::MinTileCells || settings.tileCells > NavMeshBuildSettings::MaxTileCells) {
        return "navigation tiles must be 16 to 252 cells wide";
    }
    if (!InRange(settings.edgeMaxError, 0.1F, 10.0F)) return "navigation edge error must be between 0.1 and 10 cells";
    if (settings.profiles.empty() || settings.profiles.size() > NavMeshBuildSettings::MaxProfiles) {
        return "a navigation mesh needs 1 to 8 agent profiles";
    }
    for (std::size_t index = 0U; index < settings.profiles.size(); ++index) {
        const NavAgentProfile& profile = settings.profiles[index];
        if (profile.name.empty() || profile.name.size() > NavAgentProfile::MaxNameBytes ||
            std::ranges::any_of(profile.name, [](char character) { return static_cast<unsigned char>(character) < 0x20U || character == 0x7F; })) {
            return "navigation agent profile names must be 1 to 64 printable characters";
        }
        for (std::size_t other = 0U; other < index; ++other) {
            if (settings.profiles[other].name == profile.name) return "navigation agent profile \"" + profile.name + "\" is declared twice";
        }
        if (!InRange(profile.radius, 0.0F, 50.0F)) return "agent profile \"" + profile.name + "\" needs a radius from 0 to 50 metres";
        if (!InRange(profile.height, 0.01F, 100.0F)) return "agent profile \"" + profile.name + "\" needs a height from 0.01 to 100 metres";
        if (!InRange(profile.maxClimb, 0.0F, 100.0F)) return "agent profile \"" + profile.name + "\" needs a climb from 0 to 100 metres";
        if (!InRange(profile.maxSlopeDegrees, 0.0F, 89.0F)) return "agent profile \"" + profile.name + "\" needs a slope from 0 to 89 degrees";
        const ProfileVoxels voxels = Voxels(settings, profile);
        if (voxels.height > 250 || voxels.climb > 250 || voxels.radius > 250) {
            return "agent profile \"" + profile.name + "\" is more than 250 navigation cells in size";
        }
    }
    return {};
}

bool SameNavMeshLayout(const NavMeshBuildSettings& a, const NavMeshBuildSettings& b) noexcept {
    return a.cellSize == b.cellSize && a.cellHeight == b.cellHeight && a.tileCells == b.tileCells && a.profiles == b.profiles;
}

void NavGeometry::Add(NavGeometryChunk chunk) {
    if (!std::isfinite(chunk.origin.x) || !std::isfinite(chunk.origin.y) || !std::isfinite(chunk.origin.z)) {
        return;
    }
    const std::size_t vertexCount = chunk.vertices.size() / 3U;
    chunk.vertices.resize(vertexCount * 3U);
    std::vector<std::uint32_t> indices;
    indices.reserve(chunk.indices.size() - chunk.indices.size() % 3U);
    const auto usable = [&](std::uint32_t vertex) {
        return vertex < vertexCount && std::isfinite(chunk.vertices[vertex * 3U]) && std::isfinite(chunk.vertices[vertex * 3U + 1U]) &&
            std::isfinite(chunk.vertices[vertex * 3U + 2U]);
    };
    for (std::size_t triangle = 0U; triangle + 2U < chunk.indices.size(); triangle += 3U) {
        if (usable(chunk.indices[triangle]) && usable(chunk.indices[triangle + 1U]) && usable(chunk.indices[triangle + 2U])) {
            indices.insert(indices.end(), chunk.indices.begin() + static_cast<std::ptrdiff_t>(triangle),
                chunk.indices.begin() + static_cast<std::ptrdiff_t>(triangle + 3U));
        }
    }
    if (indices.empty()) {
        return;
    }
    chunk.indices = std::move(indices);
    chunks_.push_back(std::move(chunk));
}

void NavGeometry::AddBakeBox(const NavBakeBox& box) {
    const bool finite = std::isfinite(box.min.x) && std::isfinite(box.min.y) && std::isfinite(box.min.z) &&
        std::isfinite(box.max.x) && std::isfinite(box.max.y) && std::isfinite(box.max.z);
    if (finite && box.min.x <= box.max.x && box.min.y <= box.max.y && box.min.z <= box.max.z) {
        bakeBoxes_.push_back(box);
    }
}

std::size_t NavGeometry::TriangleCount() const noexcept {
    std::size_t count = 0U;
    for (const NavGeometryChunk& chunk : chunks_) {
        count += chunk.indices.size() / 3U;
    }
    return count;
}

bool NavMeshBuilder::BuildTile(const NavMeshBuildSettings& settings, std::uint32_t profile, NavTileCoord coord,
    const NavGeometry& geometry, NavTile& tile, std::string& error) {
    if (std::string invalid = ValidateNavMeshBuildSettings(settings); !invalid.empty()) {
        error = std::move(invalid);
        return false;
    }
    if (profile >= settings.profiles.size()) {
        error = "the navigation mesh has no agent profile " + std::to_string(profile);
        return false;
    }
    std::vector<TriangleRef> triangles;
    const double tileSize = settings.TileWorldSize();
    const double border = BorderWorld(settings);
    const std::span<const NavGeometryChunk> chunks = geometry.Chunks();
    for (std::uint32_t chunkIndex = 0U; chunkIndex < chunks.size(); ++chunkIndex) {
        const auto triangleCount = static_cast<std::uint32_t>(chunks[chunkIndex].indices.size() / 3U);
        for (std::uint32_t triangle = 0U; triangle < triangleCount; ++triangle) {
            NavTileCoord low{};
            NavTileCoord high{};
            if (TriangleTiles(chunks[chunkIndex], triangle, tileSize, border, low, high) && coord.x >= low.x && coord.x <= high.x &&
                coord.z >= low.z && coord.z <= high.z) {
                triangles.push_back({ chunkIndex, triangle });
            }
        }
    }
    return BuildTileFromTriangles(settings, profile, coord, geometry, triangles, tile, error);
}

std::size_t NavMeshBuilder::DefaultBakeWorkerCount() noexcept {
    return std::min<std::size_t>(8U, kb::ecs::WorkerPool::DefaultWorkerCount());
}

NavMeshBakeResult NavMeshBuilder::Bake(const NavMeshBuildSettings& settings, const NavGeometry& geometry, kb::ecs::WorkerPool* workers,
    NavBakeProgress* progress) {
    const auto start = std::chrono::steady_clock::now();
    NavMeshBakeResult result;
    if (std::string invalid = ValidateNavMeshBuildSettings(settings); !invalid.empty()) {
        result.error = std::move(invalid);
        return result;
    }
    result.stats.sourceTriangles = geometry.TriangleCount();
    std::string error;
    const std::size_t maxTiles = (std::size_t{ 1 } << 20U) / settings.profiles.size();
    const std::map<NavTileCoord, std::vector<TriangleRef>> buckets = BucketTriangles(settings, geometry, maxTiles, error);
    if (!error.empty()) {
        result.error = std::move(error);
        return result;
    }
    struct Job {
        std::uint32_t profile = 0U;
        const NavTileCoord* coord = nullptr;
        const std::vector<TriangleRef>* triangles = nullptr;
    };
    std::vector<Job> jobs;
    jobs.reserve(buckets.size() * settings.profiles.size());
    for (std::uint32_t profile = 0U; profile < settings.profiles.size(); ++profile) {
        for (const auto& [coord, triangles] : buckets) {
            jobs.push_back({ profile, &coord, &triangles });
        }
    }
    std::vector<NavTile> built(jobs.size());
    std::vector<std::string> errors(jobs.size());
    std::atomic<bool> failed{ false };
    if (progress != nullptr) progress->total.store(jobs.size());
    RunTileJobs(jobs.size(), workers, [&](std::size_t job) {
        if (!failed.load() &&
            !BuildTileFromTriangles(settings, jobs[job].profile, *jobs[job].coord, geometry, *jobs[job].triangles, built[job], errors[job])) {
            failed.store(true);
        }
        if (progress != nullptr) progress->built.fetch_add(1U);
    });
    for (std::size_t job = 0U; job < jobs.size(); ++job) {
        if (!errors[job].empty()) {
            result.error = std::move(errors[job]);
            return result;
        }
    }
    for (NavTile& tile : built) {
        if (!tile.layers.empty()) {
            result.stats.layers += tile.layers.size();
            result.tiles.push_back(std::move(tile));
        }
    }
    result.stats.tiles = result.tiles.size();
    result.stats.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    result.succeeded = true;
    return result;
}

NavMeshBakeResult NavMeshBuilder::RebuildTiles(const NavMeshBuildSettings& settings, const NavGeometry& geometry, std::vector<NavTile> tiles,
    std::span<const NavTileCoord> coords, kb::ecs::WorkerPool* workers, NavBakeProgress* progress) {
    const auto start = std::chrono::steady_clock::now();
    NavMeshBakeResult result;
    if (std::string invalid = ValidateNavMeshBuildSettings(settings); !invalid.empty()) {
        result.error = std::move(invalid);
        return result;
    }
    result.stats.sourceTriangles = geometry.TriangleCount();
    const double tileSize = settings.TileWorldSize();
    const std::set<NavTileCoord> wanted(coords.begin(), coords.end());
    struct Job {
        std::uint32_t profile = 0U;
        NavTileCoord coord{};
    };
    // Tiles a bake would not build (outside every bake box) are dropped like the bake leaves them out.
    std::vector<Job> jobs;
    for (std::uint32_t profile = 0U; profile < settings.profiles.size(); ++profile) {
        for (const NavTileCoord& coord : wanted) {
            if (TileInBakeBoxes(geometry, coord, tileSize)) jobs.push_back({ profile, coord });
        }
    }
    std::erase_if(tiles, [&wanted](const NavTile& tile) { return wanted.contains(tile.coord); });
    std::vector<NavTile> built(jobs.size());
    std::vector<std::string> errors(jobs.size());
    std::atomic<bool> failed{ false };
    if (progress != nullptr) progress->total.store(jobs.size());
    RunTileJobs(jobs.size(), workers, [&](std::size_t job) {
        if (!failed.load() && !BuildTile(settings, jobs[job].profile, jobs[job].coord, geometry, built[job], errors[job])) {
            failed.store(true);
        }
        if (progress != nullptr) progress->built.fetch_add(1U);
    });
    for (std::size_t job = 0U; job < jobs.size(); ++job) {
        if (!errors[job].empty()) {
            result.error = std::move(errors[job]);
            return result;
        }
    }
    for (NavTile& tile : built) {
        if (!tile.layers.empty()) tiles.push_back(std::move(tile));
    }
    // The order a bake gives: by profile, then tile x, then tile z.
    std::ranges::sort(tiles, [](const NavTile& a, const NavTile& b) {
        return a.profile != b.profile ? a.profile < b.profile : a.coord < b.coord;
    });
    for (const NavTile& tile : tiles) result.stats.layers += tile.layers.size();
    result.tiles = std::move(tiles);
    result.stats.tiles = result.tiles.size();
    result.stats.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    result.succeeded = true;
    return result;
}

} // namespace kb::navigation
