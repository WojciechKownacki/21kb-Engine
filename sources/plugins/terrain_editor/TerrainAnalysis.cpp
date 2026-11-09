#include "TerrainAnalysis.hpp"

#include <algorithm>
#include <cmath>

namespace kb::terrain_editor {
namespace {

constexpr float kRadiansToDegrees = 57.2957795130823F;

// The plane of the triangle of one cell that holds a point, as a height and a world-space gradient.
struct SurfacePlane {
    float height = 0.0F;
    float gradientX = 0.0F;
    float gradientZ = 0.0F;
    std::uint32_t cellX = 0U;
    std::uint32_t cellZ = 0U;
};

[[nodiscard]] float VertexHeight(const kb::assets::TerrainAsset& terrain, std::uint32_t x, std::uint32_t z) noexcept {
    return terrain.heights[static_cast<std::size_t>(z) * terrain.width + x];
}

[[nodiscard]] SurfacePlane CellPlane(
    const kb::assets::TerrainAsset& terrain, std::uint32_t cellX, std::uint32_t cellZ, float fractionX, float fractionZ) noexcept {
    const float stepX = terrain.worldSizeX / static_cast<float>(terrain.width - 1U);
    const float stepZ = terrain.worldSizeZ / static_cast<float>(terrain.height - 1U);
    const float h00 = VertexHeight(terrain, cellX, cellZ);
    const float h10 = VertexHeight(terrain, cellX + 1U, cellZ);
    const float h01 = VertexHeight(terrain, cellX, cellZ + 1U);
    const float h11 = VertexHeight(terrain, cellX + 1U, cellZ + 1U);
    SurfacePlane plane{ .cellX = cellX, .cellZ = cellZ };
    if (fractionX + fractionZ <= 1.0F) {
        plane.height = h00 + (h10 - h00) * fractionX + (h01 - h00) * fractionZ;
        plane.gradientX = (h10 - h00) / stepX;
        plane.gradientZ = (h01 - h00) / stepZ;
    } else {
        plane.height = h11 + (h01 - h11) * (1.0F - fractionX) + (h10 - h11) * (1.0F - fractionZ);
        plane.gradientX = (h11 - h01) / stepX;
        plane.gradientZ = (h11 - h10) / stepZ;
    }
    return plane;
}

[[nodiscard]] SurfacePlane LocatePlane(const kb::assets::TerrainAsset& terrain, float localX, float localZ) noexcept {
    const float stepX = terrain.worldSizeX / static_cast<float>(terrain.width - 1U);
    const float stepZ = terrain.worldSizeZ / static_cast<float>(terrain.height - 1U);
    const float sampleX = std::clamp((localX + terrain.worldSizeX * 0.5F) / stepX, 0.0F, static_cast<float>(terrain.width - 1U));
    const float sampleZ = std::clamp((localZ + terrain.worldSizeZ * 0.5F) / stepZ, 0.0F, static_cast<float>(terrain.height - 1U));
    const std::uint32_t cellX = std::min(static_cast<std::uint32_t>(sampleX), terrain.width - 2U);
    const std::uint32_t cellZ = std::min(static_cast<std::uint32_t>(sampleZ), terrain.height - 2U);
    return CellPlane(terrain, cellX, cellZ, sampleX - static_cast<float>(cellX), sampleZ - static_cast<float>(cellZ));
}

[[nodiscard]] float SlopeOf(const SurfacePlane& plane) noexcept {
    return std::atan(std::sqrt(plane.gradientX * plane.gradientX + plane.gradientZ * plane.gradientZ)) * kRadiansToDegrees;
}

} // namespace

float SampleTerrainHeight(const kb::assets::TerrainAsset& terrain, float localX, float localZ) noexcept {
    return LocatePlane(terrain, localX, localZ).height;
}

float SampleTerrainSlopeDegrees(const kb::assets::TerrainAsset& terrain, float localX, float localZ) noexcept {
    return SlopeOf(LocatePlane(terrain, localX, localZ));
}

TerrainSurfaceSample SampleTerrainSurface(const kb::assets::TerrainAsset& terrain, float localX, float localZ) noexcept {
    const SurfacePlane plane = LocatePlane(terrain, localX, localZ);
    TerrainSurfaceSample sample{};
    sample.height = plane.height;
    const float length = std::sqrt(plane.gradientX * plane.gradientX + 1.0F + plane.gradientZ * plane.gradientZ);
    sample.normal = { -plane.gradientX / length, 1.0F / length, -plane.gradientZ / length };
    sample.slopeDegrees = SlopeOf(plane);
    sample.hole = terrain.holes[static_cast<std::size_t>(plane.cellZ) * (terrain.width - 1U) + plane.cellX] != 0U;
    if (terrain.materialLayers.empty() || terrain.layerWeightWidth < 2U || terrain.layerWeightHeight < 2U) return sample;
    constexpr std::uint32_t kChannels = kb::assets::TerrainAsset::MaximumMaterialLayers;
    const float weightX = std::clamp((localX + terrain.worldSizeX * 0.5F) / terrain.worldSizeX, 0.0F, 1.0F) *
        static_cast<float>(terrain.layerWeightWidth - 1U);
    const float weightZ = std::clamp((localZ + terrain.worldSizeZ * 0.5F) / terrain.worldSizeZ, 0.0F, 1.0F) *
        static_cast<float>(terrain.layerWeightHeight - 1U);
    const std::uint32_t x0 = std::min(static_cast<std::uint32_t>(weightX), terrain.layerWeightWidth - 2U);
    const std::uint32_t z0 = std::min(static_cast<std::uint32_t>(weightZ), terrain.layerWeightHeight - 2U);
    const float tx = weightX - static_cast<float>(x0);
    const float tz = weightZ - static_cast<float>(z0);
    const auto texel = [&](std::uint32_t x, std::uint32_t z, std::uint32_t channel) {
        return static_cast<float>(terrain.layerWeights[(static_cast<std::size_t>(z) * terrain.layerWeightWidth + x) * kChannels + channel]);
    };
    for (std::uint32_t channel = 0U; channel < kChannels; ++channel) {
        const float top = std::lerp(texel(x0, z0, channel), texel(x0 + 1U, z0, channel), tx);
        const float bottom = std::lerp(texel(x0, z0 + 1U, channel), texel(x0 + 1U, z0 + 1U, channel), tx);
        sample.layerWeights[channel] = std::lerp(top, bottom, tz) / 255.0F;
    }
    return sample;
}

std::uint64_t HashTerrainHeights(const kb::assets::TerrainAsset& terrain) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    const auto mix = [&hash](std::uint32_t value) {
        for (std::uint32_t byte = 0U; byte < 4U; ++byte) {
            hash ^= (value >> (byte * 8U)) & 0xFFU;
            hash *= 1099511628211ULL;
        }
    };
    mix(terrain.width);
    mix(terrain.height);
    for (const float height : terrain.heights) {
        mix(static_cast<std::uint32_t>(static_cast<std::int32_t>(std::lround(height * 1024.0F))));
    }
    return hash;
}

TerrainStats ComputeTerrainStats(const kb::assets::TerrainAsset& terrain) {
    TerrainStats stats{};
    if (terrain.heights.empty()) return stats;
    const auto [lowest, highest] = std::minmax_element(terrain.heights.begin(), terrain.heights.end());
    stats.minHeight = *lowest;
    stats.maxHeight = *highest;
    double sum = 0.0;
    for (const float height : terrain.heights) sum += height;
    stats.meanHeight = sum / static_cast<double>(terrain.heights.size());
    stats.heightHash = HashTerrainHeights(terrain);
    const auto count = [&stats](const SurfacePlane& plane) {
        const std::uint32_t bin = std::min(static_cast<std::uint32_t>(SlopeOf(plane) / 10.0F), kTerrainSlopeBins - 1U);
        ++stats.slopeHistogram[bin];
    };
    for (std::uint32_t z = 0U; z + 1U < terrain.height; ++z) {
        for (std::uint32_t x = 0U; x + 1U < terrain.width; ++x) {
            if (terrain.holes[static_cast<std::size_t>(z) * (terrain.width - 1U) + x] != 0U) {
                ++stats.holeCells;
                continue;
            }
            count(CellPlane(terrain, x, z, 0.0F, 0.0F));
            count(CellPlane(terrain, x, z, 1.0F, 1.0F));
        }
    }
    return stats;
}

} // namespace kb::terrain_editor
