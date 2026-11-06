#include "kb/render/resources/RenderTerrainMeshBuilder.hpp"

#include "resources/ForEachParallel.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace kb::render {
namespace {

[[nodiscard]] std::size_t VertexIndex(const kb::assets::TerrainAsset& terrain, std::uint32_t x, std::uint32_t z) noexcept {
    return static_cast<std::size_t>(z) * terrain.width + x;
}

[[nodiscard]] std::size_t CellIndex(const kb::assets::TerrainAsset& terrain, std::uint32_t x, std::uint32_t z) noexcept {
    return static_cast<std::size_t>(z) * (terrain.width - 1U) + x;
}

[[nodiscard]] bool CoarseCellHasHole(
    const kb::assets::TerrainAsset& terrain,
    std::uint32_t x,
    std::uint32_t z,
    std::uint32_t step) noexcept {
    const std::uint32_t lastX = std::min(x + step, terrain.width - 1U);
    const std::uint32_t lastZ = std::min(z + step, terrain.height - 1U);
    for (std::uint32_t cellZ = z; cellZ < lastZ; ++cellZ) {
        for (std::uint32_t cellX = x; cellX < lastX; ++cellX) {
            if (terrain.holes[CellIndex(terrain, cellX, cellZ)] != 0U) return true;
        }
    }
    return false;
}

// A layer whose weight stays under this (3% of the surface) in a whole chunk is not drawn there: the tail of a soft
// brush would otherwise cost a pass over every chunk it brushes, for a difference no one can see.
constexpr std::uint8_t kVisibleLayerWeight = 8U;

[[nodiscard]] bool TerrainChunkLayerHasWeight(
    const kb::assets::TerrainAsset& terrain,
    std::uint32_t chunkIndexX,
    std::uint32_t chunkIndexZ,
    std::uint32_t layer) noexcept {
    if (layer == 0U) return true;
    if (layer >= terrain.materialLayers.size() || terrain.layerWeightWidth < 2U ||
        terrain.layerWeightHeight < 2U) return false;
    const std::uint32_t terrainQuadsX = terrain.width - 1U;
    const std::uint32_t terrainQuadsZ = terrain.height - 1U;
    const std::uint32_t startTerrainX = chunkIndexX * terrain.chunkQuads;
    const std::uint32_t startTerrainZ = chunkIndexZ * terrain.chunkQuads;
    const std::uint32_t endTerrainX = std::min(startTerrainX + terrain.chunkQuads, terrainQuadsX);
    const std::uint32_t endTerrainZ = std::min(startTerrainZ + terrain.chunkQuads, terrainQuadsZ);
    const std::uint32_t maxWeightX = terrain.layerWeightWidth - 1U;
    const std::uint32_t maxWeightY = terrain.layerWeightHeight - 1U;
    const std::uint32_t minX = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(startTerrainX) * maxWeightX) / terrainQuadsX);
    const std::uint32_t minY = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(startTerrainZ) * maxWeightY) / terrainQuadsZ);
    const std::uint32_t maxX = std::min(
        maxWeightX,
        static_cast<std::uint32_t>((static_cast<std::uint64_t>(endTerrainX) * maxWeightX + terrainQuadsX - 1U) / terrainQuadsX));
    const std::uint32_t maxY = std::min(
        maxWeightY,
        static_cast<std::uint32_t>((static_cast<std::uint64_t>(endTerrainZ) * maxWeightY + terrainQuadsZ - 1U) / terrainQuadsZ));
    for (std::uint32_t y = minY; y <= maxY; ++y) {
        for (std::uint32_t x = minX; x <= maxX; ++x) {
            const std::size_t offset =
                (static_cast<std::size_t>(y) * terrain.layerWeightWidth + x) * 4U + layer;
            if (terrain.layerWeights[offset] >= kVisibleLayerWeight) return true;
        }
    }
    return false;
}

[[nodiscard]] RenderStaticMeshVertexP3N3UV2 BuildVertex(
    const kb::assets::TerrainAsset& terrain,
    std::uint32_t x,
    std::uint32_t z) noexcept {
    const float cellX = terrain.worldSizeX / static_cast<float>(terrain.width - 1U);
    const float cellZ = terrain.worldSizeZ / static_cast<float>(terrain.height - 1U);
    const std::uint32_t left = x == 0U ? x : x - 1U;
    const std::uint32_t right = std::min(x + 1U, terrain.width - 1U);
    const std::uint32_t top = z == 0U ? z : z - 1U;
    const std::uint32_t bottom = std::min(z + 1U, terrain.height - 1U);
    const float dx = std::max(cellX * static_cast<float>(right - left), 0.000001F);
    const float dz = std::max(cellZ * static_cast<float>(bottom - top), 0.000001F);
    const float heightDx = terrain.heights[VertexIndex(terrain, right, z)] - terrain.heights[VertexIndex(terrain, left, z)];
    const float heightDz = terrain.heights[VertexIndex(terrain, x, bottom)] - terrain.heights[VertexIndex(terrain, x, top)];
    float nx = -heightDx / dx;
    float ny = 1.0F;
    float nz = -heightDz / dz;
    const float normalLength = std::sqrt(nx * nx + ny * ny + nz * nz);
    nx /= normalLength;
    ny /= normalLength;
    nz /= normalLength;
    const float u = static_cast<float>(x) / static_cast<float>(terrain.width - 1U);
    const float v = static_cast<float>(z) / static_cast<float>(terrain.height - 1U);
    return RenderStaticMeshVertexP3N3UV2{
        .x = u * terrain.worldSizeX - terrain.worldSizeX * 0.5F,
        .y = terrain.heights[VertexIndex(terrain, x, z)],
        .z = v * terrain.worldSizeZ - terrain.worldSizeZ * 0.5F,
        .nx = nx, .ny = ny, .nz = nz,
        .u = u * (terrain.worldSizeX / 4.0F),
        .v = v * (terrain.worldSizeZ / 4.0F),
        .u1 = u, .v1 = v,
    };
}

[[nodiscard]] std::uint64_t TerrainTopologyKey(const kb::assets::TerrainAsset& terrain) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    const auto append = [&hash](std::uint64_t value) noexcept {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    append(terrain.width);
    append(terrain.height);
    append(terrain.chunkQuads);
    append(terrain.lodCount);
    append(std::bit_cast<std::uint32_t>(terrain.lodPixelError));
    append(std::bit_cast<std::uint32_t>(terrain.worldSizeX));
    append(std::bit_cast<std::uint32_t>(terrain.worldSizeZ));
    append(terrain.materialLayers.size());
    append(terrain.layerWeightWidth);
    append(terrain.layerWeightHeight);
    for (const kb::assets::TerrainMaterialLayer& layer : terrain.materialLayers) {
        append(layer.materialAssetId);
    }
    for (const std::uint8_t hole : terrain.holes) append(hole);
    return hash == 0U ? 1U : hash;
}

void ExpandBounds(RenderBoundsSphere& bounds, const RenderStaticMeshVertexP3N3T4UV2& vertex) noexcept {
    const float dx = vertex.x - bounds.center[0];
    const float dy = vertex.y - bounds.center[1];
    const float dz = vertex.z - bounds.center[2];
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!bounds.IsValid()) {
        bounds.center = { vertex.x, vertex.y, vertex.z };
        bounds.radius = 0.000001F;
    } else if (distance > bounds.radius) {
        const float expandedRadius = (bounds.radius + distance) * 0.5F;
        const float shift = (expandedRadius - bounds.radius) / distance;
        bounds.center[0] += dx * shift;
        bounds.center[1] += dy * shift;
        bounds.center[2] += dz * shift;
        bounds.radius = expandedRadius;
    }
}

[[nodiscard]] RenderStaticMeshVertexP3N3T4UV2 BuildTangentVertex(
    const kb::assets::TerrainAsset& terrain,
    std::uint32_t x,
    std::uint32_t z) noexcept {
    const RenderStaticMeshVertexP3N3UV2 source = BuildVertex(terrain, x, z);
    const float tangentLength = std::sqrt(source.ny * source.ny + source.nx * source.nx);
    const float tx = tangentLength > 0.000001F ? source.ny / tangentLength : 1.0F;
    const float ty = tangentLength > 0.000001F ? -source.nx / tangentLength : 0.0F;
    return RenderStaticMeshVertexP3N3T4UV2{
        .x = source.x, .y = source.y, .z = source.z,
        .nx = source.nx, .ny = source.ny, .nz = source.nz,
        .tx = tx, .ty = ty, .tz = 0.0F, .tw = 1.0F,
        .u = source.u, .v = source.v,
        .u1 = source.u1, .v1 = source.v1,
        .r = source.r, .g = source.g, .b = source.b, .a = source.a,
    };
}

constexpr std::uint32_t kTileCells = 8U;

// Chunks of a terrain mesh. Every chunk draws the part of the shared grid inside it at each level of detail; a level
// that steps over 2^level quads has a skirt, a wall hung below the border of the chunk, that hides the gap to a
// neighbour drawn at another level. The vertices of the skirts follow the grid in the vertex buffer, one run per
// chunk with a vertex for every full-resolution vertex of its border.
struct ChunkLayout {
    std::uint32_t quadsX = 0U;
    std::uint32_t quadsZ = 0U;
    std::uint32_t chunkQuads = 0U;
    std::uint32_t countX = 0U;
    std::uint32_t countZ = 0U;
    std::uint32_t lodCount = 0U;
    std::uint32_t ringStride = 0U;
    std::uint32_t gridVertices = 0U;
    float cellSize = 0.0F;
    [[nodiscard]] std::uint32_t Count() const noexcept { return countX * countZ; }
};

[[nodiscard]] ChunkLayout MakeChunkLayout(const kb::assets::TerrainAsset& terrain) noexcept {
    ChunkLayout layout{};
    layout.quadsX = terrain.width - 1U;
    layout.quadsZ = terrain.height - 1U;
    layout.chunkQuads = terrain.chunkQuads;
    layout.countX = (layout.quadsX + terrain.chunkQuads - 1U) / terrain.chunkQuads;
    layout.countZ = (layout.quadsZ + terrain.chunkQuads - 1U) / terrain.chunkQuads;
    // A level steps over 2^level quads; the smallest chunk must hold at least one cell of the coarsest level.
    const std::uint32_t smallestExtent = std::min({ terrain.chunkQuads, layout.quadsX, layout.quadsZ });
    layout.lodCount = std::min(terrain.lodCount, static_cast<std::uint32_t>(std::bit_width(smallestExtent)));
    layout.ringStride = 4U * terrain.chunkQuads;
    layout.gridVertices = terrain.width * terrain.height;
    layout.cellSize = std::min(
        terrain.worldSizeX / static_cast<float>(layout.quadsX), terrain.worldSizeZ / static_cast<float>(layout.quadsZ));
    return layout;
}

[[nodiscard]] std::size_t TotalVertexCount(const ChunkLayout& layout) noexcept {
    return static_cast<std::size_t>(layout.gridVertices) + static_cast<std::size_t>(layout.Count()) * layout.ringStride;
}

// Vertex ranges of a chunk: x0..x1 and z0..z1 are vertex coordinates, so neighbouring chunks share their border.
struct ChunkRange {
    std::uint32_t x0 = 0U;
    std::uint32_t x1 = 0U;
    std::uint32_t z0 = 0U;
    std::uint32_t z1 = 0U;
    [[nodiscard]] std::uint32_t Width() const noexcept { return x1 - x0; }
    [[nodiscard]] std::uint32_t Depth() const noexcept { return z1 - z0; }
    [[nodiscard]] std::uint32_t Perimeter() const noexcept { return 2U * (Width() + Depth()); }
};

[[nodiscard]] ChunkRange RangeOfChunk(const ChunkLayout& layout, std::uint32_t chunk) noexcept {
    const std::uint32_t chunkX = chunk % layout.countX;
    const std::uint32_t chunkZ = chunk / layout.countX;
    return ChunkRange{
        .x0 = chunkX * layout.chunkQuads,
        .x1 = std::min((chunkX + 1U) * layout.chunkQuads, layout.quadsX),
        .z0 = chunkZ * layout.chunkQuads,
        .z1 = std::min((chunkZ + 1U) * layout.chunkQuads, layout.quadsZ),
    };
}

// The way round the chunk: its south edge towards +x, the east edge towards +z, the north edge towards -x and the
// west edge towards -z. A skirt segment between two of its vertices faces away from the chunk.
[[nodiscard]] std::pair<std::uint32_t, std::uint32_t> PerimeterVertex(const ChunkRange& range, std::uint32_t position) noexcept {
    const std::uint32_t width = range.Width();
    const std::uint32_t depth = range.Depth();
    if (position < width) return { range.x0 + position, range.z0 };
    position -= width;
    if (position < depth) return { range.x1, range.z0 + position };
    position -= depth;
    if (position < width) return { range.x1 - position, range.z1 };
    position -= width;
    return { range.x0, range.z1 - position };
}

// A side of the chunk has a skirt only where another chunk lies beyond it.
struct SkirtSides {
    bool south = false;
    bool east = false;
    bool north = false;
    bool west = false;
};

[[nodiscard]] SkirtSides SidesWithNeighbour(const ChunkLayout& layout, std::uint32_t chunk) noexcept {
    const std::uint32_t chunkX = chunk % layout.countX;
    const std::uint32_t chunkZ = chunk / layout.countX;
    return SkirtSides{
        .south = chunkZ > 0U,
        .east = chunkX + 1U < layout.countX,
        .north = chunkZ + 1U < layout.countZ,
        .west = chunkX > 0U,
    };
}

// How far one level of a chunk strays from the full-resolution surface, over every vertex of the chunk and over its
// border alone (the border decides how far a skirt has to hang).
struct LevelError {
    float all = 0.0F;
    float border = 0.0F;
};

[[nodiscard]] LevelError MeasureLevelError(
    const kb::assets::TerrainAsset& terrain,
    const ChunkRange& range,
    std::uint32_t step) noexcept {
    LevelError error{};
    const std::uint32_t cellsX = range.Width() / step;
    const std::uint32_t cellsZ = range.Depth() / step;
    const float inverseStep = 1.0F / static_cast<float>(step);
    for (std::uint32_t z = range.z0; z <= range.z1; ++z) {
        const std::uint32_t cellZ = std::min((z - range.z0) / step, cellsZ - 1U);
        const std::uint32_t cellZ0 = range.z0 + cellZ * step;
        const float v = static_cast<float>(z - cellZ0) * inverseStep;
        for (std::uint32_t x = range.x0; x <= range.x1; ++x) {
            const std::uint32_t cellX = std::min((x - range.x0) / step, cellsX - 1U);
            const std::uint32_t cellX0 = range.x0 + cellX * step;
            const float u = static_cast<float>(x - cellX0) * inverseStep;
            const float a = terrain.heights[VertexIndex(terrain, cellX0, cellZ0)];
            const float b = terrain.heights[VertexIndex(terrain, cellX0 + step, cellZ0)];
            const float c = terrain.heights[VertexIndex(terrain, cellX0, cellZ0 + step)];
            const float d = terrain.heights[VertexIndex(terrain, cellX0 + step, cellZ0 + step)];
            // The cell is split along b-c, as the triangles of the mesh are.
            const float level = u + v <= 1.0F
                ? a + u * (b - a) + v * (c - a)
                : d + (1.0F - u) * (c - d) + (1.0F - v) * (b - d);
            const float distance = std::abs(terrain.heights[VertexIndex(terrain, x, z)] - level);
            error.all = std::max(error.all, distance);
            if (x == range.x0 || x == range.x1 || z == range.z0 || z == range.z1) error.border = std::max(error.border, distance);
        }
    }
    return error;
}

struct ChunkErrors {
    std::array<float, 8U> level{};
    float border = 0.0F;
};

// The errors of every level of one chunk. They never decrease with the level, so a chunk that looks right at a
// coarse level also looks right at a finer one.
[[nodiscard]] ChunkErrors MeasureChunkErrors(
    const kb::assets::TerrainAsset& terrain,
    const ChunkLayout& layout,
    std::uint32_t chunk) noexcept {
    ChunkErrors errors{};
    const ChunkRange range = RangeOfChunk(layout, chunk);
    for (std::uint32_t lod = 1U; lod < layout.lodCount; ++lod) {
        const LevelError measured = MeasureLevelError(terrain, range, 1U << lod);
        errors.level[lod] = std::max(errors.level[lod - 1U], measured.all);
        errors.border = std::max(errors.border, measured.border);
    }
    return errors;
}

void StoreChunkErrors(RenderTerrainChunkGrid& grid, std::uint32_t chunk, const ChunkErrors& errors) noexcept {
    for (std::uint32_t lod = 0U; lod < grid.lodCount; ++lod) grid.lodError[static_cast<std::size_t>(chunk) * grid.lodCount + lod] = errors.level[lod];
    grid.borderError[chunk] = errors.border;
}

// Two chunks at different levels part, at their shared border, by at most the sum of the errors of the two borders.
[[nodiscard]] float SkirtDepthOfChunk(
    const ChunkLayout& layout,
    const RenderTerrainChunkGrid& grid,
    std::uint32_t chunk) noexcept {
    const std::uint32_t chunkX = chunk % layout.countX;
    const std::uint32_t chunkZ = chunk / layout.countX;
    float neighbour = 0.0F;
    if (chunkX > 0U) neighbour = std::max(neighbour, grid.borderError[chunk - 1U]);
    if (chunkX + 1U < layout.countX) neighbour = std::max(neighbour, grid.borderError[chunk + 1U]);
    if (chunkZ > 0U) neighbour = std::max(neighbour, grid.borderError[chunk - layout.countX]);
    if (chunkZ + 1U < layout.countZ) neighbour = std::max(neighbour, grid.borderError[chunk + layout.countX]);
    const float depth = grid.borderError[chunk] + neighbour;
    // A hair more than the sum, as the heights are floats; and never less than half a cell, so that even where the
    // two borders agree the wall is there for the pixels the rasteriser leaves between triangles that meet in a T.
    return std::max(depth + layout.cellSize * 0.001F, layout.cellSize * 0.5F);
}

// The skirt vertices of one chunk: a copy of every vertex of its border, hung `depth` below it.
template <typename Vertex>
void WriteChunkRing(
    const kb::assets::TerrainAsset& terrain,
    const ChunkLayout& layout,
    std::uint32_t chunk,
    float depth,
    Vertex* vertices) noexcept {
    const ChunkRange range = RangeOfChunk(layout, chunk);
    Vertex* ring = vertices + layout.gridVertices + static_cast<std::size_t>(chunk) * layout.ringStride;
    const std::uint32_t length = range.Perimeter();
    for (std::uint32_t position = 0U; position < length; ++position) {
        const auto [x, z] = PerimeterVertex(range, position);
        Vertex vertex = vertices[VertexIndex(terrain, x, z)];
        vertex.y -= depth;
        ring[position] = vertex;
    }
}

// Calls visit(position, next, topA, topB) for every segment of the skirt of a chunk at the level whose cells span `step`
// quads: its place on the way round the chunk and the grid vertices of its top. A segment beside a hole has no
// skirt: nothing is there to hide a gap behind.
template <typename Visit>
void ForEachSkirtSegment(
    const kb::assets::TerrainAsset& terrain,
    const ChunkLayout& layout,
    std::uint32_t chunk,
    std::uint32_t step,
    Visit&& visit) noexcept {
    const ChunkRange range = RangeOfChunk(layout, chunk);
    const SkirtSides sides = SidesWithNeighbour(layout, chunk);
    const std::uint32_t width = range.Width();
    const std::uint32_t depth = range.Depth();
    const std::uint32_t length = range.Perimeter();
    const std::array<std::array<std::uint32_t, 3U>, 4U> runs{ {
        { sides.south ? 1U : 0U, 0U, width },
        { sides.east ? 1U : 0U, width, depth },
        { sides.north ? 1U : 0U, width + depth, width },
        { sides.west ? 1U : 0U, 2U * width + depth, depth },
    } };
    for (const auto& run : runs) {
        if (run[0] == 0U) continue;
        for (std::uint32_t position = run[1]; position < run[1] + run[2]; position += step) {
            const std::uint32_t next = position + step == length ? 0U : position + step;
            const auto [xa, za] = PerimeterVertex(range, position);
            const auto [xb, zb] = PerimeterVertex(range, next);
            // The cell inside the chunk that the segment is the edge of.
            const bool alongX = za == zb;
            const std::uint32_t cellX = alongX ? std::min(xa, xb) : (xa == range.x0 ? range.x0 : xa - step);
            const std::uint32_t cellZ = alongX ? (za == range.z0 ? range.z0 : za - step) : std::min(za, zb);
            if (CoarseCellHasHole(terrain, cellX, cellZ, step)) continue;
            visit(position, next, static_cast<std::uint32_t>(VertexIndex(terrain, xa, za)), static_cast<std::uint32_t>(VertexIndex(terrain, xb, zb)));
        }
    }
}

// Number of indices of the skirt of a chunk at the level whose cells span `step` quads.
[[nodiscard]] std::uint32_t SkirtIndexCount(
    const kb::assets::TerrainAsset& terrain, const ChunkLayout& layout, std::uint32_t chunk, std::uint32_t step) noexcept {
    std::uint32_t indices = 0U;
    ForEachSkirtSegment(terrain, layout, chunk, step, [&indices](std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) { indices += 6U; });
    return indices;
}

// Writes the triangles of that skirt, facing away from the chunk.
void WriteChunkSkirt(
    const kb::assets::TerrainAsset& terrain,
    const ChunkLayout& layout,
    std::uint32_t chunk,
    std::uint32_t step,
    std::uint32_t* out) noexcept {
    const std::uint32_t ringBase = layout.gridVertices + chunk * layout.ringStride;
    ForEachSkirtSegment(terrain, layout, chunk, step, [&](std::uint32_t position, std::uint32_t next, std::uint32_t topA, std::uint32_t topB) {
        *out++ = topA; *out++ = topB; *out++ = ringBase + position;
        *out++ = topB; *out++ = ringBase + next; *out++ = ringBase + position;
    });
}

// The bounds of a chunk, its skirt included, from the heights of its vertices.
[[nodiscard]] RenderBoundsSphere ChunkBounds(
    const kb::assets::TerrainAsset& terrain,
    const ChunkRange& range,
    float skirtDepth) noexcept {
    float minimum = std::numeric_limits<float>::max();
    float maximum = std::numeric_limits<float>::lowest();
    for (std::uint32_t z = range.z0; z <= range.z1; ++z) {
        for (std::uint32_t x = range.x0; x <= range.x1; ++x) {
            const float height = terrain.heights[VertexIndex(terrain, x, z)];
            minimum = std::min(minimum, height);
            maximum = std::max(maximum, height);
        }
    }
    const float cellX = terrain.worldSizeX / static_cast<float>(terrain.width - 1U);
    const float cellZ = terrain.worldSizeZ / static_cast<float>(terrain.height - 1U);
    const float minX = static_cast<float>(range.x0) * cellX - terrain.worldSizeX * 0.5F;
    const float maxX = static_cast<float>(range.x1) * cellX - terrain.worldSizeX * 0.5F;
    const float minZ = static_cast<float>(range.z0) * cellZ - terrain.worldSizeZ * 0.5F;
    const float maxZ = static_cast<float>(range.z1) * cellZ - terrain.worldSizeZ * 0.5F;
    const float halfX = (maxX - minX) * 0.5F;
    const float halfY = (maximum - (minimum - skirtDepth)) * 0.5F;
    const float halfZ = (maxZ - minZ) * 0.5F;
    return RenderBoundsSphere{
        .center = { minX + halfX, minimum - skirtDepth + halfY, minZ + halfZ },
        .radius = std::max(std::sqrt(halfX * halfX + halfY * halfY + halfZ * halfZ), 0.000001F),
    };
}

} // namespace

std::optional<RenderMeshAssetData> RenderTerrainMeshBuilder::Build(const kb::assets::TerrainAsset& terrain) {
    if (!kb::assets::IsTerrainAssetValid(terrain)) return std::nullopt;

    const ChunkLayout layout = MakeChunkLayout(terrain);
    const std::uint32_t chunkCount = layout.Count();
    const std::uint32_t lodCount = layout.lodCount;

    RenderMeshAssetData mesh{};
    mesh.vertices.resize(TotalVertexCount(layout));
    ForEachParallel(terrain.height, [&](std::size_t row) {
        for (std::uint32_t x = 0U; x < terrain.width; ++x) {
            mesh.vertices[row * terrain.width + x] = BuildVertex(terrain, x, static_cast<std::uint32_t>(row));
        }
    });

    // How far each level of each chunk strays from the surface, and so how far a skirt has to hang.
    RenderTerrainChunkGrid grid{};
    grid.chunkCountX = layout.countX;
    grid.chunkCountZ = layout.countZ;
    grid.lodCount = lodCount;
    grid.lodPixelError = terrain.lodPixelError;
    grid.lodError.assign(static_cast<std::size_t>(chunkCount) * lodCount, 0.0F);
    grid.borderError.assign(chunkCount, 0.0F);
    grid.skirtDepth.assign(chunkCount, 0.0F);
    ForEachParallel(chunkCount, [&](std::size_t chunk) {
        StoreChunkErrors(grid, static_cast<std::uint32_t>(chunk), MeasureChunkErrors(terrain, layout, static_cast<std::uint32_t>(chunk)));
    });
    ForEachParallel(chunkCount, [&](std::size_t chunk) {
        grid.skirtDepth[chunk] = SkirtDepthOfChunk(layout, grid, static_cast<std::uint32_t>(chunk));
        WriteChunkRing(terrain, layout, static_cast<std::uint32_t>(chunk), grid.skirtDepth[chunk], mesh.vertices.data());
    });

    const std::uint32_t materialLayerCount = std::max<std::uint32_t>(
        1U, static_cast<std::uint32_t>(terrain.materialLayers.size()));
    std::vector<std::uint8_t> chunkLayerActive(static_cast<std::size_t>(chunkCount) * materialLayerCount, 1U);
    ForEachParallel(chunkCount, [&](std::size_t chunk) {
        for (std::uint32_t layer = 1U; layer < materialLayerCount; ++layer) {
            chunkLayerActive[chunk * materialLayerCount + layer] = TerrainChunkLayerHasWeight(
                terrain, static_cast<std::uint32_t>(chunk) % layout.countX, static_cast<std::uint32_t>(chunk) / layout.countX,
                layer) ? 1U : 0U;
        }
    });

    // The triangles of every chunk at every level, the skirt of that chunk and level behind them. Counting first
    // lets every chunk write its own range.
    const std::size_t taskCount = static_cast<std::size_t>(lodCount) * chunkCount;
    std::vector<std::uint32_t> mainIndexCount(taskCount);
    std::vector<std::uint32_t> skirtIndexCount(taskCount);
    ForEachParallel(taskCount, [&](std::size_t task) {
        const std::uint32_t step = 1U << (task / chunkCount);
        const std::uint32_t chunk = static_cast<std::uint32_t>(task % chunkCount);
        const ChunkRange range = RangeOfChunk(layout, chunk);
        std::uint32_t cells = 0U;
        for (std::uint32_t z = range.z0; z + step <= range.z1; z += step) {
            for (std::uint32_t x = range.x0; x + step <= range.x1; x += step) {
                if (!CoarseCellHasHole(terrain, x, z, step)) ++cells;
            }
        }
        mainIndexCount[task] = cells * 6U;
        skirtIndexCount[task] = cells == 0U ? 0U : SkirtIndexCount(terrain, layout, chunk, step);
    });
    std::vector<std::size_t> firstIndex(taskCount + 1U);
    for (std::size_t task = 0U; task < taskCount; ++task) {
        firstIndex[task + 1U] = firstIndex[task] + mainIndexCount[task] + skirtIndexCount[task];
    }
    mesh.indices32.resize(firstIndex[taskCount]);
    ForEachParallel(taskCount, [&](std::size_t task) {
        const std::uint32_t step = 1U << (task / chunkCount);
        const std::uint32_t chunk = static_cast<std::uint32_t>(task % chunkCount);
        const ChunkRange range = RangeOfChunk(layout, chunk);
        std::uint32_t* out = mesh.indices32.data() + firstIndex[task];
        // Cells in tiles of kTileCells by kTileCells: the vertices of a tile stay in the cache of the GPU while it is drawn.
        const std::uint32_t tile = kTileCells * step;
        for (std::uint32_t tileZ = range.z0; tileZ < range.z1; tileZ += tile) {
            for (std::uint32_t tileX = range.x0; tileX < range.x1; tileX += tile) {
                for (std::uint32_t z = tileZ; z < std::min(tileZ + tile, range.z1); z += step) {
                    for (std::uint32_t x = tileX; x < std::min(tileX + tile, range.x1); x += step) {
                        if (x + step > range.x1 || z + step > range.z1 || CoarseCellHasHole(terrain, x, z, step)) continue;
                        const std::uint32_t a = static_cast<std::uint32_t>(VertexIndex(terrain, x, z));
                        const std::uint32_t b = static_cast<std::uint32_t>(VertexIndex(terrain, x + step, z));
                        const std::uint32_t c = static_cast<std::uint32_t>(VertexIndex(terrain, x, z + step));
                        const std::uint32_t d = static_cast<std::uint32_t>(VertexIndex(terrain, x + step, z + step));
                        *out++ = a; *out++ = c; *out++ = b;
                        *out++ = b; *out++ = c; *out++ = d;
                    }
                }
            }
        }
        if (skirtIndexCount[task] != 0U) WriteChunkSkirt(terrain, layout, chunk, step, out);
    });

    grid.sectionIndices.assign(taskCount, UINT32_MAX);
    grid.skirtSectionIndices.assign(taskCount, UINT32_MAX);
    const std::uint8_t skirtLayer = terrain.materialLayers.empty() ? static_cast<std::uint8_t>(UINT8_MAX) : std::uint8_t{ 0U };
    for (std::uint32_t lod = 0U; lod < lodCount; ++lod) {
        for (std::uint32_t chunk = 0U; chunk < chunkCount; ++chunk) {
            const std::size_t task = static_cast<std::size_t>(lod) * chunkCount + chunk;
            if (mainIndexCount[task] == 0U) continue;
            grid.sectionIndices[task] = static_cast<std::uint32_t>(mesh.sections.size());
            for (std::uint32_t layer = 0U; layer < materialLayerCount; ++layer) {
                mesh.sections.push_back(RenderMeshSectionDesc{
                    .indexStart = static_cast<std::uint32_t>(firstIndex[task]),
                    .indexCount = mainIndexCount[task],
                    .materialSlot = layer,
                    .lodLevel = static_cast<std::uint8_t>(lod),
                    .terrainLayerIndex = terrain.materialLayers.empty()
                        ? static_cast<std::uint8_t>(UINT8_MAX)
                        : static_cast<std::uint8_t>(layer),
                    .terrainLayerActive = chunkLayerActive[static_cast<std::size_t>(chunk) * materialLayerCount + layer] != 0U,
                    .terrainChunk = chunk,
                });
            }
            if (skirtIndexCount[task] == 0U) continue;
            grid.skirtSectionIndices[task] = static_cast<std::uint32_t>(mesh.sections.size());
            mesh.sections.push_back(RenderMeshSectionDesc{
                .indexStart = static_cast<std::uint32_t>(firstIndex[task] + mainIndexCount[task]),
                .indexCount = skirtIndexCount[task],
                .materialSlot = 0U,
                .lodLevel = static_cast<std::uint8_t>(lod),
                .terrainLayerIndex = skirtLayer,
                .terrainChunk = chunk,
                .terrainSkirt = true,
            });
        }
    }
    if (mesh.indices32.empty() || !RenderMeshAssetBuilder::Finalize(
            mesh, RenderMeshFinalizeOptions{ .optimizeVertexFetch = false, .optimizeVertexCache = false })) return std::nullopt;
    const std::vector<float> skirtDepths = grid.skirtDepth;
    mesh.terrainChunks = std::make_shared<const RenderTerrainChunkGrid>(std::move(grid));
    // Finalize derived the tangents of the grid from the triangles; a skirt vertex wears the tangent of the vertex
    // it hangs from.
    ForEachParallel(chunkCount, [&](std::size_t chunk) {
        WriteChunkRing(terrain, layout, static_cast<std::uint32_t>(chunk), skirtDepths[chunk], mesh.tangentVertices.data());
    });
    if (terrain.materialLayers.empty()) {
        mesh.materialSlots.push_back(RenderMaterialSlotDesc{});
        mesh.materialNames.emplace_back("Terrain");
        RenderMeshEmbeddedMaterial defaultMaterial{};
        defaultMaterial.name = "Terrain";
        defaultMaterial.desc.baseColor[0] = 1.0F;
        defaultMaterial.desc.baseColor[1] = 1.0F;
        defaultMaterial.desc.baseColor[2] = 1.0F;
        defaultMaterial.desc.baseColor[3] = 1.0F;
        defaultMaterial.desc.roughnessFactor = 0.92F;
        mesh.embeddedMaterials.push_back(std::move(defaultMaterial));
    } else {
        mesh.materialSlots.reserve(terrain.materialLayers.size());
        mesh.materialNames.reserve(terrain.materialLayers.size());
        for (std::uint32_t layer = 0U; layer < terrain.materialLayers.size(); ++layer) {
            mesh.materialSlots.push_back(RenderMaterialSlotDesc{
                .defaultMaterialAssetId = terrain.materialLayers[layer].materialAssetId,
            });
            mesh.materialNames.push_back("Terrain Layer " + std::to_string(layer + 1U));
        }
        mesh.terrainLayerWeights = terrain.layerWeights;
        mesh.terrainLayerWeightWidth = static_cast<std::uint16_t>(terrain.layerWeightWidth);
        mesh.terrainLayerWeightHeight = static_cast<std::uint16_t>(terrain.layerWeightHeight);
        mesh.terrainLayerCount = static_cast<std::uint8_t>(terrain.materialLayers.size());
    }
    mesh.dynamicTopologyKey = TerrainTopologyKey(terrain);
    mesh.RefreshDesc();
    return mesh;
}

bool RenderTerrainMeshBuilder::PrepareDynamicPreview(
    const kb::assets::TerrainAsset& terrain,
    RenderMeshAssetData& mesh) noexcept {
    if (!kb::assets::IsTerrainAssetValid(terrain)) return false;
    const std::size_t vertexCount = TotalVertexCount(MakeChunkLayout(terrain));
    if (mesh.tangentVertices.size() != vertexCount || mesh.terrainChunks == nullptr || !mesh.terrainChunks->Valid() ||
        mesh.dynamicTopologyKey != TerrainTopologyKey(terrain)) {
        return false;
    }
    mesh.dynamicVertexUpdates = true;
    mesh.vertexUpdateFirst = 0U;
    mesh.vertexUpdateCount = static_cast<std::uint32_t>(vertexCount);
    mesh.dynamicVertexUpdateRanges.clear();
    mesh.dynamicVertexUpdateRanges.push_back(RenderMeshVertexUpdateRange{
        .firstVertex = mesh.vertexUpdateFirst,
        .vertexCount = mesh.vertexUpdateCount,
    });
    mesh.dynamicSectionUpdateIndices.clear();
    mesh.dynamicSectionUpdateIndices.reserve(mesh.sections.size());
    for (std::uint32_t sectionIndex = 0U; sectionIndex < mesh.sections.size(); ++sectionIndex) {
        mesh.dynamicSectionUpdateIndices.push_back(sectionIndex);
    }
    mesh.dynamicTerrainLayerWeightUpdates.clear();
    if (mesh.terrainLayerCount != 0U) {
        mesh.dynamicTerrainLayerWeightUpdates.push_back(RenderTerrainLayerWeightUpdateRegion{
            .width = mesh.terrainLayerWeightWidth,
            .height = mesh.terrainLayerWeightHeight,
        });
    }
    mesh.RefreshDesc();
    return true;
}

bool RenderTerrainMeshBuilder::UpdateDynamicPreview(
    const kb::assets::TerrainAsset& terrain,
    const RenderTerrainMeshUpdateRegion& region,
    RenderMeshAssetData& mesh) noexcept {
    const ChunkLayout layout = MakeChunkLayout(terrain);
    if (!mesh.dynamicVertexUpdates || mesh.tangentVertices.size() != TotalVertexCount(layout) ||
        mesh.terrainChunks == nullptr || !mesh.terrainChunks->Valid() || mesh.terrainChunks->ChunkCount() != layout.Count() ||
        region.minX > region.maxX || region.minZ > region.maxZ ||
        region.maxX >= terrain.width || region.maxZ >= terrain.height) {
        return false;
    }
    RenderTerrainChunkGrid grid = *mesh.terrainChunks;
    const std::uint32_t minX = region.minX == 0U ? 0U : region.minX - 1U;
    const std::uint32_t minZ = region.minZ == 0U ? 0U : region.minZ - 1U;
    const std::uint32_t maxX = std::min(region.maxX + 1U, terrain.width - 1U);
    const std::uint32_t maxZ = std::min(region.maxZ + 1U, terrain.height - 1U);
    float minimumHeight = std::numeric_limits<float>::max();
    float maximumHeight = std::numeric_limits<float>::lowest();
    for (std::uint32_t z = minZ; z <= maxZ; ++z) {
        for (std::uint32_t x = minX; x <= maxX; ++x) {
            RenderStaticMeshVertexP3N3T4UV2& vertex = mesh.tangentVertices[VertexIndex(terrain, x, z)];
            vertex = BuildTangentVertex(terrain, x, z);
            minimumHeight = std::min(minimumHeight, vertex.y);
            maximumHeight = std::max(maximumHeight, vertex.y);
        }
    }
    mesh.vertexUpdateFirst = minZ * terrain.width + minX;
    const std::uint32_t updateWidth = maxX - minX + 1U;
    mesh.vertexUpdateCount = updateWidth * (maxZ - minZ + 1U);
    for (std::uint32_t z = minZ; z <= maxZ; ++z) {
        mesh.dynamicVertexUpdateRanges.push_back(RenderMeshVertexUpdateRange{
            .firstVertex = z * terrain.width + minX,
            .vertexCount = updateWidth,
        });
    }

    // The chunks that hold a changed vertex have new errors; the chunks next to them hang their skirts by those.
    const std::uint32_t changedMinX = minX == 0U ? 0U : (minX - 1U) / terrain.chunkQuads;
    const std::uint32_t changedMinZ = minZ == 0U ? 0U : (minZ - 1U) / terrain.chunkQuads;
    const std::uint32_t changedMaxX = std::min(maxX / terrain.chunkQuads, layout.countX - 1U);
    const std::uint32_t changedMaxZ = std::min(maxZ / terrain.chunkQuads, layout.countZ - 1U);
    for (std::uint32_t chunkZ = changedMinZ; chunkZ <= changedMaxZ; ++chunkZ) {
        for (std::uint32_t chunkX = changedMinX; chunkX <= changedMaxX; ++chunkX) {
            const std::uint32_t chunk = chunkZ * layout.countX + chunkX;
            StoreChunkErrors(grid, chunk, MeasureChunkErrors(terrain, layout, chunk));
        }
    }
    const std::uint32_t touchedMinX = changedMinX == 0U ? 0U : changedMinX - 1U;
    const std::uint32_t touchedMinZ = changedMinZ == 0U ? 0U : changedMinZ - 1U;
    const std::uint32_t touchedMaxX = std::min(changedMaxX + 1U, layout.countX - 1U);
    const std::uint32_t touchedMaxZ = std::min(changedMaxZ + 1U, layout.countZ - 1U);
    const std::uint32_t layers = std::max<std::uint32_t>(mesh.terrainLayerCount, 1U);
    std::uint32_t ringVertices = 0U;
    float touchedDepth = 0.0F;
    for (std::uint32_t chunkZ = touchedMinZ; chunkZ <= touchedMaxZ; ++chunkZ) {
        for (std::uint32_t chunkX = touchedMinX; chunkX <= touchedMaxX; ++chunkX) {
            const std::uint32_t chunk = chunkZ * layout.countX + chunkX;
            const ChunkRange range = RangeOfChunk(layout, chunk);
            const float previousDepth = grid.skirtDepth[chunk];
            grid.skirtDepth[chunk] = SkirtDepthOfChunk(layout, grid, chunk);
            touchedDepth = std::max(touchedDepth, grid.skirtDepth[chunk]);
            const bool holdsChange = chunkX >= changedMinX && chunkX <= changedMaxX && chunkZ >= changedMinZ && chunkZ <= changedMaxZ;
            // A neighbour whose skirt keeps its depth has nothing new to upload.
            if (!holdsChange && grid.skirtDepth[chunk] == previousDepth) continue;
            WriteChunkRing(terrain, layout, chunk, grid.skirtDepth[chunk], mesh.tangentVertices.data());
            mesh.dynamicVertexUpdateRanges.push_back(RenderMeshVertexUpdateRange{
                .firstVertex = layout.gridVertices + chunk * layout.ringStride,
                .vertexCount = range.Perimeter(),
            });
            ringVertices += range.Perimeter();
            const RenderBoundsSphere bounds = ChunkBounds(terrain, range, grid.skirtDepth[chunk]);
            for (std::uint32_t lod = 0U; lod < grid.lodCount; ++lod) {
                const std::size_t task = static_cast<std::size_t>(lod) * grid.ChunkCount() + chunk;
                const std::uint32_t first = grid.sectionIndices[task];
                if (first == UINT32_MAX) continue;
                const std::uint32_t skirt = grid.skirtSectionIndices[task];
                for (std::uint32_t layer = 0U; layer <= layers; ++layer) {
                    const std::uint32_t sectionIndex = layer < layers ? first + layer : skirt;
                    if (sectionIndex == UINT32_MAX || sectionIndex >= mesh.sections.size()) continue;
                    mesh.sections[sectionIndex].bounds = bounds;
                    if (sectionIndex < mesh.meshlets.size()) mesh.meshlets[sectionIndex].bounds = bounds;
                    mesh.dynamicSectionUpdateIndices.push_back(sectionIndex);
                }
            }
        }
    }
    mesh.vertexUpdateCount += ringVertices;

    const float worldMinX = static_cast<float>(minX) * (terrain.worldSizeX / static_cast<float>(terrain.width - 1U)) - terrain.worldSizeX * 0.5F;
    const float worldMaxX = static_cast<float>(maxX) * (terrain.worldSizeX / static_cast<float>(terrain.width - 1U)) - terrain.worldSizeX * 0.5F;
    const float worldMinZ = static_cast<float>(minZ) * (terrain.worldSizeZ / static_cast<float>(terrain.height - 1U)) - terrain.worldSizeZ * 0.5F;
    const float worldMaxZ = static_cast<float>(maxZ) * (terrain.worldSizeZ / static_cast<float>(terrain.height - 1U)) - terrain.worldSizeZ * 0.5F;
    const float lowest = minimumHeight - touchedDepth;
    const std::array<RenderStaticMeshVertexP3N3T4UV2, 8U> corners{
        RenderStaticMeshVertexP3N3T4UV2{ .x = worldMinX, .y = lowest, .z = worldMinZ },
        RenderStaticMeshVertexP3N3T4UV2{ .x = worldMaxX, .y = lowest, .z = worldMinZ },
        RenderStaticMeshVertexP3N3T4UV2{ .x = worldMinX, .y = lowest, .z = worldMaxZ },
        RenderStaticMeshVertexP3N3T4UV2{ .x = worldMaxX, .y = lowest, .z = worldMaxZ },
        RenderStaticMeshVertexP3N3T4UV2{ .x = worldMinX, .y = maximumHeight, .z = worldMinZ },
        RenderStaticMeshVertexP3N3T4UV2{ .x = worldMaxX, .y = maximumHeight, .z = worldMinZ },
        RenderStaticMeshVertexP3N3T4UV2{ .x = worldMinX, .y = maximumHeight, .z = worldMaxZ },
        RenderStaticMeshVertexP3N3T4UV2{ .x = worldMaxX, .y = maximumHeight, .z = worldMaxZ },
    };
    for (const RenderStaticMeshVertexP3N3T4UV2& corner : corners) ExpandBounds(mesh.bounds, corner);
    mesh.terrainChunks = std::make_shared<const RenderTerrainChunkGrid>(std::move(grid));
    mesh.RefreshDesc();
    return true;
}

bool RenderTerrainMeshBuilder::UpdateDynamicLayerPreview(
    const kb::assets::TerrainAsset& terrain,
    const RenderTerrainLayerWeightUpdateRegion& region,
    RenderMeshAssetData& mesh) noexcept {
    if (terrain.materialLayers.empty() ||
        mesh.terrainLayerCount != terrain.materialLayers.size() ||
        mesh.terrainLayerWeightWidth != terrain.layerWeightWidth ||
        mesh.terrainLayerWeightHeight != terrain.layerWeightHeight ||
        mesh.terrainLayerWeights.size() != terrain.layerWeights.size() ||
        region.width == 0U || region.height == 0U ||
        static_cast<std::uint32_t>(region.x) + region.width > terrain.layerWeightWidth ||
        static_cast<std::uint32_t>(region.y) + region.height > terrain.layerWeightHeight) {
        return false;
    }
    const std::size_t rowBytes = static_cast<std::size_t>(region.width) * 4U;
    for (std::uint16_t row = 0U; row < region.height; ++row) {
        const std::size_t offset =
            (static_cast<std::size_t>(region.y + row) * terrain.layerWeightWidth + region.x) * 4U;
        std::copy_n(
            terrain.layerWeights.data() + offset,
            rowBytes,
            mesh.terrainLayerWeights.data() + offset);
    }
    const std::uint32_t terrainQuadsX = terrain.width - 1U;
    const std::uint32_t terrainQuadsZ = terrain.height - 1U;
    const std::uint32_t maxWeightX = terrain.layerWeightWidth - 1U;
    const std::uint32_t maxWeightY = terrain.layerWeightHeight - 1U;
    const std::uint32_t minTerrainX = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(region.x) * terrainQuadsX) / maxWeightX);
    const std::uint32_t minTerrainZ = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(region.y) * terrainQuadsZ) / maxWeightY);
    const std::uint32_t maxTerrainX = std::min(
        terrainQuadsX,
        static_cast<std::uint32_t>((static_cast<std::uint64_t>(region.x + region.width) * terrainQuadsX + maxWeightX - 1U) / maxWeightX));
    const std::uint32_t maxTerrainZ = std::min(
        terrainQuadsZ,
        static_cast<std::uint32_t>((static_cast<std::uint64_t>(region.y + region.height) * terrainQuadsZ + maxWeightY - 1U) / maxWeightY));
    if (mesh.terrainChunks == nullptr) return false;
    const RenderTerrainChunkGrid& grid = *mesh.terrainChunks;
    const std::uint32_t minChunkX = std::min(minTerrainX / terrain.chunkQuads, grid.chunkCountX - 1U);
    const std::uint32_t minChunkZ = std::min(minTerrainZ / terrain.chunkQuads, grid.chunkCountZ - 1U);
    const std::uint32_t maxChunkX = std::min(maxTerrainX / terrain.chunkQuads, grid.chunkCountX - 1U);
    const std::uint32_t maxChunkZ = std::min(maxTerrainZ / terrain.chunkQuads, grid.chunkCountZ - 1U);
    for (std::uint32_t lod = 0U; lod < grid.lodCount; ++lod) {
        for (std::uint32_t chunkZ = minChunkZ; chunkZ <= maxChunkZ; ++chunkZ) {
            for (std::uint32_t chunkX = minChunkX; chunkX <= maxChunkX; ++chunkX) {
                const std::size_t task = static_cast<std::size_t>(lod) * grid.ChunkCount() +
                    static_cast<std::size_t>(chunkZ) * grid.chunkCountX + chunkX;
                const std::uint32_t firstSection = grid.sectionIndices[task];
                if (firstSection == UINT32_MAX) continue;
                for (std::uint32_t layer = 1U; layer < mesh.terrainLayerCount; ++layer) {
                    const std::uint32_t sectionIndex = firstSection + layer;
                    if (sectionIndex >= mesh.sections.size()) continue;
                    const bool active = TerrainChunkLayerHasWeight(terrain, chunkX, chunkZ, layer);
                    if (mesh.sections[sectionIndex].terrainLayerActive != active) {
                        mesh.sections[sectionIndex].terrainLayerActive = active;
                        mesh.dynamicSectionUpdateIndices.push_back(sectionIndex);
                    }
                }
            }
        }
    }
    mesh.dynamicTerrainLayerWeightUpdates.push_back(region);
    mesh.RefreshDesc();
    return true;
}

} // namespace kb::render
