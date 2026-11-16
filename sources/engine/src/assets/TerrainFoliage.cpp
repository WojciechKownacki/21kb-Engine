#include "engine/assets/TerrainFoliage.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace kb::assets {
namespace {

[[nodiscard]] std::uint64_t Mix(std::uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

// Five independent values in [0, 1) for one lattice cell: accept, jitter x, jitter z, yaw, scale.
struct CellRandom {
    std::array<float, 5> value{};
};

[[nodiscard]] CellRandom RandomForCell(std::uint64_t chunkSeed, std::int64_t cellX, std::int64_t cellZ) noexcept {
    std::uint64_t state = Mix(chunkSeed ^ Mix(static_cast<std::uint64_t>(cellX)) ^
        (Mix(static_cast<std::uint64_t>(cellZ)) << 1U));
    CellRandom random{};
    for (float& value : random.value) {
        state = Mix(state);
        value = static_cast<float>(state >> 40U) * (1.0F / 16777216.0F);
    }
    return random;
}

// x and z are measured from the terrain corner.
[[nodiscard]] float HeightAt(const TerrainAsset& terrain, float x, float z) noexcept {
    const float fx = std::clamp(x / terrain.worldSizeX, 0.0F, 1.0F) * static_cast<float>(terrain.width - 1U);
    const float fz = std::clamp(z / terrain.worldSizeZ, 0.0F, 1.0F) * static_cast<float>(terrain.height - 1U);
    const std::uint32_t x0 = std::min(static_cast<std::uint32_t>(fx), terrain.width - 2U);
    const std::uint32_t z0 = std::min(static_cast<std::uint32_t>(fz), terrain.height - 2U);
    const float tx = fx - static_cast<float>(x0);
    const float tz = fz - static_cast<float>(z0);
    const auto at = [&](std::uint32_t ix, std::uint32_t iz) {
        return terrain.heights[static_cast<std::size_t>(iz) * terrain.width + ix];
    };
    const float top = std::lerp(at(x0, z0), at(x0 + 1U, z0), tx);
    const float bottom = std::lerp(at(x0, z0 + 1U), at(x0 + 1U, z0 + 1U), tx);
    return std::lerp(top, bottom, tz);
}

[[nodiscard]] bool InHole(const TerrainAsset& terrain, float x, float z) noexcept {
    const std::uint32_t quadX = std::min(
        static_cast<std::uint32_t>(x / terrain.worldSizeX * static_cast<float>(terrain.width - 1U)), terrain.width - 2U);
    const std::uint32_t quadZ = std::min(
        static_cast<std::uint32_t>(z / terrain.worldSizeZ * static_cast<float>(terrain.height - 1U)), terrain.height - 2U);
    return terrain.holes[static_cast<std::size_t>(quadZ) * (terrain.width - 1U) + quadX] != 0U;
}

[[nodiscard]] std::array<float, 16> BuildModel(
    const TerrainAsset& terrain, const TerrainFoliageLayer& layer, const CellRandom& random, float x, float z) noexcept {
    const float step = std::max(terrain.worldSizeX / static_cast<float>(terrain.width - 1U), 0.01F);
    const float dx = HeightAt(terrain, x + step, z) - HeightAt(terrain, std::max(x - step, 0.0F), z);
    const float dz = HeightAt(terrain, x, z + step) - HeightAt(terrain, x, std::max(z - step, 0.0F));
    const float normalLength = std::sqrt(dx * dx + 4.0F * step * step + dz * dz);
    const std::array<float, 3> normal{ -dx / normalLength, 2.0F * step / normalLength, -dz / normalLength };
    std::array<float, 3> up{
        std::lerp(0.0F, normal[0], layer.alignToNormal),
        std::lerp(1.0F, normal[1], layer.alignToNormal),
        std::lerp(0.0F, normal[2], layer.alignToNormal) };
    const float upLength = std::sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
    for (float& component : up) component /= upLength;
    const float yaw = random.value[3] * layer.rotationRangeDegrees * (std::numbers::pi_v<float> / 180.0F);
    const std::array<float, 3> reference{ std::sin(yaw), 0.0F, std::cos(yaw) };
    const float along = reference[0] * up[0] + reference[1] * up[1] + reference[2] * up[2];
    std::array<float, 3> forward{ reference[0] - up[0] * along, reference[1] - up[1] * along, reference[2] - up[2] * along };
    const float forwardLength = std::sqrt(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
    for (float& component : forward) component /= forwardLength;
    const std::array<float, 3> right{
        up[1] * forward[2] - up[2] * forward[1],
        up[2] * forward[0] - up[0] * forward[2],
        up[0] * forward[1] - up[1] * forward[0] };
    const float scale = std::lerp(layer.minScale, layer.maxScale, random.value[4]);
    return {
        right[0] * scale, right[1] * scale, right[2] * scale, 0.0F,
        up[0] * scale, up[1] * scale, up[2] * scale, 0.0F,
        forward[0] * scale, forward[1] * scale, forward[2] * scale, 0.0F,
        x - terrain.worldSizeX * 0.5F, HeightAt(terrain, x, z), z - terrain.worldSizeZ * 0.5F, 1.0F };
}

} // namespace

TerrainFoliageChunkCoord TerrainFoliageChunkCount(const TerrainAsset& terrain) noexcept {
    return {
        static_cast<std::uint32_t>(std::ceil(terrain.worldSizeX / kTerrainFoliageChunkMeters)),
        static_cast<std::uint32_t>(std::ceil(terrain.worldSizeZ / kTerrainFoliageChunkMeters)) };
}

TerrainFoliageChunk BuildTerrainFoliageChunk(
    const TerrainAsset& terrain, std::uint32_t layerIndex, TerrainFoliageChunkCoord coord) {
    TerrainFoliageChunk chunk{ .layer = layerIndex, .coord = coord };
    const std::size_t planeSize = static_cast<std::size_t>(terrain.foliageMapWidth) * terrain.foliageMapHeight;
    if (layerIndex >= terrain.foliageLayers.size() || planeSize == 0U ||
        terrain.foliageDensity.size() != planeSize * terrain.foliageLayers.size()) {
        return chunk;
    }
    const TerrainFoliageLayer& layer = terrain.foliageLayers[layerIndex];
    const std::uint8_t* plane = terrain.foliageDensity.data() + planeSize * layerIndex;
    // The plants sit on a jittered lattice; the paint thins it out instead of moving points around.
    const float cell = 1.0F / std::sqrt(layer.density);
    const std::uint64_t chunkSeed = Mix(layer.seed ^ Mix(layerIndex) ^
        (Mix(coord.x) << 1U) ^ (Mix(coord.z) << 2U));
    const auto firstCell = [&](std::uint32_t chunkIndex) {
        return static_cast<std::int64_t>(std::floor(static_cast<float>(chunkIndex) * kTerrainFoliageChunkMeters / cell)) - 1;
    };
    // A cell belongs to the chunk its lattice origin falls in, so no cell is placed twice or missed.
    const auto chunkOfCell = [&](std::int64_t cellIndex) {
        return static_cast<std::int64_t>(std::floor(static_cast<float>(cellIndex) * cell / kTerrainFoliageChunkMeters));
    };
    const std::int64_t lastX = firstCell(coord.x + 1U) + 2;
    const std::int64_t lastZ = firstCell(coord.z + 1U) + 2;
    std::array<float, 3> minimum{ 1.0e30F, 1.0e30F, 1.0e30F };
    std::array<float, 3> maximum{ -1.0e30F, -1.0e30F, -1.0e30F };
    for (std::int64_t cellZ = std::max<std::int64_t>(firstCell(coord.z), 0); cellZ <= lastZ; ++cellZ) {
        if (chunkOfCell(cellZ) != static_cast<std::int64_t>(coord.z)) continue;
        for (std::int64_t cellX = std::max<std::int64_t>(firstCell(coord.x), 0); cellX <= lastX; ++cellX) {
            if (chunkOfCell(cellX) != static_cast<std::int64_t>(coord.x)) continue;
            const CellRandom random = RandomForCell(chunkSeed, cellX, cellZ);
            const float x = (static_cast<float>(cellX) + random.value[1]) * cell;
            const float z = (static_cast<float>(cellZ) + random.value[2]) * cell;
            if (x >= terrain.worldSizeX || z >= terrain.worldSizeZ) continue;
            const std::uint32_t texelX = static_cast<std::uint32_t>(std::lround(
                x / terrain.worldSizeX * static_cast<float>(terrain.foliageMapWidth - 1U)));
            const std::uint32_t texelZ = static_cast<std::uint32_t>(std::lround(
                z / terrain.worldSizeZ * static_cast<float>(terrain.foliageMapHeight - 1U)));
            const float paint = static_cast<float>(
                plane[static_cast<std::size_t>(texelZ) * terrain.foliageMapWidth + texelX]) / 255.0F;
            if (random.value[0] >= paint || InHole(terrain, x, z)) continue;
            TerrainFoliageInstance instance{ .model = BuildModel(terrain, layer, random, x, z) };
            for (std::size_t axis = 0U; axis < 3U; ++axis) {
                minimum[axis] = std::min(minimum[axis], instance.model[12U + axis]);
                maximum[axis] = std::max(maximum[axis], instance.model[12U + axis]);
            }
            chunk.instances.push_back(instance);
        }
    }
    if (!chunk.instances.empty()) {
        float radiusSquared = 0.0F;
        for (std::size_t axis = 0U; axis < 3U; ++axis) {
            chunk.center[axis] = (minimum[axis] + maximum[axis]) * 0.5F;
            const float extent = (maximum[axis] - minimum[axis]) * 0.5F;
            radiusSquared += extent * extent;
        }
        chunk.radius = std::sqrt(radiusSquared);
    }
    return chunk;
}

std::vector<TerrainFoliageChunk> BuildTerrainFoliage(const TerrainAsset& terrain) {
    std::vector<TerrainFoliageChunk> chunks;
    const TerrainFoliageChunkCoord count = TerrainFoliageChunkCount(terrain);
    for (std::uint32_t layer = 0U; layer < terrain.foliageLayers.size(); ++layer) {
        for (std::uint32_t z = 0U; z < count.z; ++z) {
            for (std::uint32_t x = 0U; x < count.x; ++x) {
                TerrainFoliageChunk chunk = BuildTerrainFoliageChunk(terrain, layer, { x, z });
                if (!chunk.instances.empty()) chunks.push_back(std::move(chunk));
            }
        }
    }
    return chunks;
}

} // namespace kb::assets
