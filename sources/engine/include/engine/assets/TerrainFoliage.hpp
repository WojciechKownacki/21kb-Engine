#pragma once

#include "engine/assets/TerrainAsset.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace kb::assets {

// Foliage is placed per chunk of the terrain, in a square of this many metres. A chunk is also the unit
// the renderer draws and culls, so a terrain with a million plants stays a few thousand batches.
inline constexpr float kTerrainFoliageChunkMeters = 32.0F;

struct TerrainFoliageChunkCoord {
    std::uint32_t x = 0U;
    std::uint32_t z = 0U;
};

struct TerrainFoliageInstance {
    // Column-major model matrix in terrain space (the terrain centre is the origin).
    std::array<float, 16> model{};
};

struct TerrainFoliageChunk {
    std::uint32_t layer = 0U;
    TerrainFoliageChunkCoord coord{};
    // Sphere around every instance origin; the renderer grows it by the plant mesh bounds.
    std::array<float, 3> center{};
    float radius = 0.0F;
    std::vector<TerrainFoliageInstance> instances;
};

[[nodiscard]] TerrainFoliageChunkCoord TerrainFoliageChunkCount(const TerrainAsset& terrain) noexcept;

// Places the plants of one layer in one chunk. The same terrain data always gives the same instances,
// in the editor and in the packaged game. Instances come out in a fixed order. A chunk without any
// plant comes back with no instances.
[[nodiscard]] TerrainFoliageChunk BuildTerrainFoliageChunk(
    const TerrainAsset& terrain,
    std::uint32_t layerIndex,
    TerrainFoliageChunkCoord coord);

// Every chunk of every layer that holds at least one plant, layer by layer.
[[nodiscard]] std::vector<TerrainFoliageChunk> BuildTerrainFoliage(const TerrainAsset& terrain);

} // namespace kb::assets
