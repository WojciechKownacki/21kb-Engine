#pragma once

#include "engine/assets/TerrainAsset.hpp"

#include <array>
#include <cstdint>

namespace kb::terrain_editor {

// What a terrain looks like at one point of its local plane (x, z measured from its centre).
// Heights and normals follow the rendered surface: each cell is two triangles split from its
// (x+1, z) corner to its (x, z+1) corner, the split the mesh builder uses.
struct TerrainSurfaceSample {
    float height = 0.0F;
    std::array<float, 3> normal{ 0.0F, 1.0F, 0.0F };
    float slopeDegrees = 0.0F;
    bool hole = false;
    // Material layer weights as fractions (sum 1); all zero when the terrain has no layers.
    std::array<float, kb::assets::TerrainAsset::MaximumMaterialLayers> layerWeights{};
};

inline constexpr std::uint32_t kTerrainSlopeBins = 9U;

struct TerrainStats {
    float minHeight = 0.0F;
    float maxHeight = 0.0F;
    double meanHeight = 0.0;
    std::uint32_t holeCells = 0U;
    // Triangles by slope in 10 degree bins (0-10 ... 80-90), holes left out.
    std::array<std::uint32_t, kTerrainSlopeBins> slopeHistogram{};
    std::uint64_t heightHash = 0U;
};

// Terrain positions outside the asset clamp to its border.
[[nodiscard]] float SampleTerrainHeight(const kb::assets::TerrainAsset& terrain, float localX, float localZ) noexcept;
[[nodiscard]] float SampleTerrainSlopeDegrees(const kb::assets::TerrainAsset& terrain, float localX, float localZ) noexcept;
[[nodiscard]] TerrainSurfaceSample SampleTerrainSurface(const kb::assets::TerrainAsset& terrain, float localX, float localZ) noexcept;
[[nodiscard]] TerrainStats ComputeTerrainStats(const kb::assets::TerrainAsset& terrain);
// FNV-1a over the heights in steps of 1/1024 unit, so the hash is stable against last-bit
// differences between builds and equal for equal terrain.
[[nodiscard]] std::uint64_t HashTerrainHeights(const kb::assets::TerrainAsset& terrain) noexcept;

} // namespace kb::terrain_editor
