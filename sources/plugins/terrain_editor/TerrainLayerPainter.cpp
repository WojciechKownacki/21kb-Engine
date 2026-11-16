#include "TerrainLayerPainter.hpp"

#include "TerrainAnalysis.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace kb::terrain_editor {
namespace {

constexpr std::uint32_t kChannels = kb::assets::TerrainAsset::MaximumMaterialLayers;

void MarkChanged(TerrainLayerPaintResult& result, std::uint32_t x, std::uint32_t y) noexcept {
    ++result.changedTexels;
    result.minX = std::min(result.minX, x);
    result.minY = std::min(result.minY, y);
    result.maxX = std::max(result.maxX, x);
    result.maxY = std::max(result.maxY, y);
}

void NormalizeAfterTargetChange(
    std::uint8_t* weights,
    std::uint32_t layerCount,
    std::uint32_t targetLayer,
    std::uint32_t targetWeight) noexcept {
    targetWeight = std::min(targetWeight, 255U);
    const std::uint32_t remaining = 255U - targetWeight;
    const std::uint32_t oldOtherTotal = 255U - weights[targetLayer];
    std::array<std::uint32_t, kChannels> scaled{};
    std::uint32_t assigned = 0U;
    if (oldOtherTotal != 0U) {
        for (std::uint32_t layer = 0U; layer < layerCount; ++layer) {
            if (layer == targetLayer) continue;
            scaled[layer] = (static_cast<std::uint32_t>(weights[layer]) * remaining) / oldOtherTotal;
            assigned += scaled[layer];
        }
        for (std::uint32_t layer = 0U; assigned < remaining && layer < layerCount; ++layer) {
            if (layer != targetLayer) {
                ++scaled[layer];
                ++assigned;
            }
        }
    } else if (remaining != 0U && layerCount > 1U) {
        const std::uint32_t fallback = targetLayer == 0U ? 1U : 0U;
        scaled[fallback] = remaining;
    }
    for (std::uint32_t layer = 0U; layer < kChannels; ++layer) {
        weights[layer] = static_cast<std::uint8_t>(
            layer == targetLayer ? targetWeight : scaled[layer]);
    }
}

// Everything a swept brush stroke needs, whichever map it paints.
struct SweptBrush {
    TerrainBrushShape shape = TerrainBrushShape::SoftRound;
    float radius = 8.0F;
    float opacity = 1.0F;
    float falloff = 0.65F;
    std::uint32_t noiseSeed = 1U;
};

// Calls `visit(x, y, worldX, worldZ, influence)` for every texel of a width x height map that spans the
// terrain edge to edge and that the brush swept from `start` to `end` touches. The stroke is rasterized
// once: replaying interpolated point stamps would multiply both opacity and work in the overlapping part
// of a stroke, making the result input-sampling dependent.
template <typename Visit>
void ForEachSweptBrushTexel(
    const kb::assets::TerrainAsset& terrain,
    std::uint32_t mapWidth,
    std::uint32_t mapHeight,
    const SweptBrush& swept,
    const TerrainBrushStamp& start,
    const TerrainBrushStamp& end,
    Visit&& visit) noexcept {
    if (!std::isfinite(terrain.worldSizeX) || terrain.worldSizeX <= 0.0F ||
        !std::isfinite(terrain.worldSizeZ) || terrain.worldSizeZ <= 0.0F ||
        !std::isfinite(swept.radius) || swept.radius <= 0.0F || swept.radius > 100000.0F ||
        !std::isfinite(swept.opacity) || swept.opacity < 0.0F ||
        !std::isfinite(swept.falloff) || swept.falloff < 0.0F || swept.falloff > 1.0F ||
        !std::isfinite(start.localX) || !std::isfinite(start.localZ) ||
        !std::isfinite(start.pressure) || start.pressure < 0.0F ||
        !std::isfinite(end.localX) || !std::isfinite(end.localZ) ||
        !std::isfinite(end.pressure) || end.pressure < 0.0F) {
        return;
    }
    const float texelSizeX = terrain.worldSizeX / static_cast<float>(mapWidth - 1U);
    const float texelSizeY = terrain.worldSizeZ / static_cast<float>(mapHeight - 1U);
    const float weightOriginX = -terrain.worldSizeX * 0.5F;
    const float weightOriginY = -terrain.worldSizeZ * 0.5F;
    const double terrainMaximumX = static_cast<double>(weightOriginX) + terrain.worldSizeX;
    const double terrainMaximumY = static_cast<double>(weightOriginY) + terrain.worldSizeZ;
    const double sweptMinimumX = std::min<double>(start.localX, end.localX) - swept.radius;
    const double sweptMinimumY = std::min<double>(start.localZ, end.localZ) - swept.radius;
    const double sweptMaximumX = std::max<double>(start.localX, end.localX) + swept.radius;
    const double sweptMaximumY = std::max<double>(start.localZ, end.localZ) + swept.radius;
    if (sweptMaximumX < weightOriginX || sweptMaximumY < weightOriginY ||
        sweptMinimumX > terrainMaximumX || sweptMinimumY > terrainMaximumY) {
        return;
    }
    const int minX = static_cast<int>(std::floor(
        (std::max<double>(sweptMinimumX, weightOriginX) - weightOriginX) / texelSizeX));
    const int minY = static_cast<int>(std::floor(
        (std::max<double>(sweptMinimumY, weightOriginY) - weightOriginY) / texelSizeY));
    const int maxX = std::min(
        static_cast<int>(mapWidth) - 1,
        static_cast<int>(std::ceil(
            (std::min(sweptMaximumX, terrainMaximumX) - weightOriginX) / texelSizeX)));
    const int maxY = std::min(
        static_cast<int>(mapHeight) - 1,
        static_cast<int>(std::ceil(
            (std::min(sweptMaximumY, terrainMaximumY) - weightOriginY) / texelSizeY)));
    const float radiusSquared = swept.radius * swept.radius;
    const double segmentX = static_cast<double>(end.localX) - start.localX;
    const double segmentY = static_cast<double>(end.localZ) - start.localZ;
    const double segmentLengthSquared = segmentX * segmentX + segmentY * segmentY;
    const TerrainBrushSettings brush{
        .shape = swept.shape,
        .radius = swept.radius,
        .strength = 1.0F,
        .falloff = swept.falloff,
        .noiseSeed = swept.noiseSeed,
    };
    for (int y = minY; y <= maxY; ++y) {
        const float worldZ = static_cast<float>(y) * texelSizeY + weightOriginY;
        for (int x = minX; x <= maxX; ++x) {
            const float worldX = static_cast<float>(x) * texelSizeX + weightOriginX;
            const double fromStartX = static_cast<double>(worldX) - start.localX;
            const double fromStartY = static_cast<double>(worldZ) - start.localZ;
            const double segmentT = segmentLengthSquared > std::numeric_limits<double>::epsilon()
                ? std::clamp((fromStartX * segmentX + fromStartY * segmentY) / segmentLengthSquared, 0.0, 1.0)
                : 0.0;
            const double dx = static_cast<double>(worldX) -
                (static_cast<double>(start.localX) + segmentX * segmentT);
            const double dz = static_cast<double>(worldZ) -
                (static_cast<double>(start.localZ) + segmentY * segmentT);
            const double distanceSquared = dx * dx + dz * dz;
            if (distanceSquared > radiusSquared) continue;
            const float pressure = std::lerp(start.pressure, end.pressure, static_cast<float>(segmentT));
            const float opacity = std::clamp(swept.opacity * pressure, 0.0F, 1.0F);
            const float influence = TerrainBrushWeight(
                static_cast<float>(std::sqrt(distanceSquared)), static_cast<std::uint32_t>(x),
                static_cast<std::uint32_t>(y), brush) * opacity;
            if (influence <= 0.0F) continue;
            visit(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), worldX, worldZ, influence);
        }
    }
}

} // namespace

bool AddTerrainMaterialLayer(
    kb::assets::TerrainAsset& terrain,
    std::uint64_t materialAssetId) noexcept {
    if (terrain.materialLayers.size() >= kb::assets::TerrainAsset::MaximumMaterialLayers) return false;
    if (terrain.materialLayers.empty()) {
        terrain.layerWeightWidth = kb::assets::TerrainAsset::DefaultLayerWeightResolution;
        terrain.layerWeightHeight = kb::assets::TerrainAsset::DefaultLayerWeightResolution;
        const std::size_t texelCount = static_cast<std::size_t>(terrain.layerWeightWidth) * terrain.layerWeightHeight;
        terrain.layerWeights.assign(texelCount * kChannels, 0U);
        for (std::size_t texel = 0U; texel < texelCount; ++texel) {
            terrain.layerWeights[texel * kChannels] = 255U;
        }
    }
    terrain.materialLayers.push_back(kb::assets::TerrainMaterialLayer{ .materialAssetId = materialAssetId });
    return true;
}

bool RemoveTerrainMaterialLayer(
    kb::assets::TerrainAsset& terrain,
    std::uint32_t layerIndex) noexcept {
    const std::uint32_t oldCount = static_cast<std::uint32_t>(terrain.materialLayers.size());
    if (layerIndex >= oldCount) return false;
    terrain.materialLayers.erase(terrain.materialLayers.begin() + static_cast<std::ptrdiff_t>(layerIndex));
    if (terrain.materialLayers.empty()) {
        terrain.layerWeightWidth = 0U;
        terrain.layerWeightHeight = 0U;
        terrain.layerWeights.clear();
        return true;
    }
    const std::uint32_t newCount = oldCount - 1U;
    for (std::size_t texel = 0U; texel < terrain.layerWeights.size(); texel += kChannels) {
        std::uint8_t* weights = terrain.layerWeights.data() + texel;
        const std::uint32_t removedWeight = weights[layerIndex];
        for (std::uint32_t layer = layerIndex; layer + 1U < oldCount; ++layer) {
            weights[layer] = weights[layer + 1U];
        }
        weights[newCount] = 0U;
        weights[0] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(weights[0]) + removedWeight);
        for (std::uint32_t layer = newCount; layer < kChannels; ++layer) weights[layer] = 0U;
    }
    return true;
}

bool SetTerrainMaterialLayer(
    kb::assets::TerrainAsset& terrain,
    std::uint32_t layerIndex,
    std::uint64_t materialAssetId) noexcept {
    if (layerIndex >= terrain.materialLayers.size()) return false;
    terrain.materialLayers[layerIndex].materialAssetId = materialAssetId;
    return true;
}

TerrainLayerPaintResult ApplyTerrainAutoPaint(
    kb::assets::TerrainAsset& terrain,
    const TerrainAutoPaintSettings& settings) noexcept {
    TerrainLayerPaintResult result{};
    const std::uint32_t layerCount = static_cast<std::uint32_t>(terrain.materialLayers.size());
    if (settings.layerIndex >= layerCount || terrain.layerWeightWidth < 2U || terrain.layerWeightHeight < 2U ||
        terrain.layerWeights.size() != static_cast<std::size_t>(terrain.layerWeightWidth) * terrain.layerWeightHeight * kChannels ||
        !kb::assets::IsTerrainResolutionValid(terrain.width) || !kb::assets::IsTerrainResolutionValid(terrain.height) ||
        terrain.heights.size() != static_cast<std::size_t>(terrain.width) * terrain.height ||
        !std::isfinite(settings.minSlopeDegrees) || !std::isfinite(settings.maxSlopeDegrees) ||
        !std::isfinite(settings.minHeight) || !std::isfinite(settings.maxHeight) ||
        settings.minSlopeDegrees > settings.maxSlopeDegrees || settings.minHeight > settings.maxHeight ||
        !std::isfinite(settings.slopeBlendDegrees) || settings.slopeBlendDegrees < 0.0F ||
        !std::isfinite(settings.heightBlend) || settings.heightBlend < 0.0F ||
        !std::isfinite(settings.opacity) || settings.opacity < 0.0F || settings.opacity > 1.0F) {
        return result;
    }
    // 1 inside [low, high], falling to 0 over `blend` outside either end.
    const auto band = [](float value, float low, float high, float blend) {
        if (value >= low && value <= high) return 1.0F;
        const float outside = value < low ? low - value : value - high;
        if (blend <= 0.0F || outside >= blend) return 0.0F;
        const float t = 1.0F - outside / blend;
        return t * t * (3.0F - 2.0F * t);
    };
    for (std::uint32_t y = 0U; y < terrain.layerWeightHeight; ++y) {
        const float worldZ = static_cast<float>(y) / static_cast<float>(terrain.layerWeightHeight - 1U) * terrain.worldSizeZ -
            terrain.worldSizeZ * 0.5F;
        for (std::uint32_t x = 0U; x < terrain.layerWeightWidth; ++x) {
            const float worldX = static_cast<float>(x) / static_cast<float>(terrain.layerWeightWidth - 1U) * terrain.worldSizeX -
                terrain.worldSizeX * 0.5F;
            const TerrainSurfaceSample sample = SampleTerrainSurface(terrain, worldX, worldZ);
            const float influence = settings.opacity *
                band(sample.slopeDegrees, settings.minSlopeDegrees, settings.maxSlopeDegrees, settings.slopeBlendDegrees) *
                band(sample.height, settings.minHeight, settings.maxHeight, settings.heightBlend);
            if (influence <= 0.0F) continue;
            std::uint8_t* weights = terrain.layerWeights.data() +
                (static_cast<std::size_t>(y) * terrain.layerWeightWidth + x) * kChannels;
            const std::uint32_t before = weights[settings.layerIndex];
            const std::uint32_t after = static_cast<std::uint32_t>(std::lround(
                std::lerp(static_cast<float>(before), 255.0F, influence)));
            if (after == before) continue;
            NormalizeAfterTargetChange(weights, layerCount, settings.layerIndex, after);
            MarkChanged(result, x, y);
        }
    }
    return result;
}

TerrainLayerPaintResult ApplyTerrainLayerPaint(
    kb::assets::TerrainAsset& terrain,
    const TerrainLayerPaintSettings& settings,
    const TerrainBrushStamp& stamp) noexcept {
    return ApplyTerrainLayerPaintSegment(terrain, settings, stamp, stamp);
}

TerrainLayerPaintResult ApplyTerrainLayerPaintSegment(
    kb::assets::TerrainAsset& terrain,
    const TerrainLayerPaintSettings& settings,
    const TerrainBrushStamp& start,
    const TerrainBrushStamp& end) noexcept {
    TerrainLayerPaintResult result{};
    const std::uint32_t layerCount = static_cast<std::uint32_t>(terrain.materialLayers.size());
    if (settings.layerIndex >= layerCount || terrain.layerWeightWidth < 2U || terrain.layerWeightHeight < 2U ||
        terrain.layerWeights.size() != static_cast<std::size_t>(terrain.layerWeightWidth) * terrain.layerWeightHeight * kChannels) {
        return result;
    }
    if (settings.erase && layerCount == 1U) return result;
    ForEachSweptBrushTexel(
        terrain, terrain.layerWeightWidth, terrain.layerWeightHeight,
        SweptBrush{ settings.shape, settings.radius, settings.opacity, settings.falloff, settings.noiseSeed },
        start, end,
        [&](std::uint32_t x, std::uint32_t y, float worldX, float worldZ, float influence) {
            std::uint8_t* weights = terrain.layerWeights.data() +
                (static_cast<std::size_t>(y) * terrain.layerWeightWidth + x) * kChannels;
            const std::uint32_t layer = settings.autoLayer && !settings.erase
                ? (SampleTerrainSlopeDegrees(terrain, worldX, worldZ) >= kTerrainAutoRockSlopeDegrees ? layerCount - 1U : 0U)
                : settings.layerIndex;
            const std::uint32_t before = weights[layer];
            const float target = settings.erase ? 0.0F : 255.0F;
            const std::uint32_t after = static_cast<std::uint32_t>(std::lround(
                std::lerp(static_cast<float>(before), target, influence)));
            if (after == before) return;
            NormalizeAfterTargetChange(weights, layerCount, layer, after);
            MarkChanged(result, x, y);
        });
    return result;
}

bool AddTerrainFoliageLayer(
    kb::assets::TerrainAsset& terrain,
    const kb::assets::TerrainFoliageLayer& layer) noexcept {
    if (terrain.foliageLayers.size() >= kb::assets::TerrainAsset::MaximumFoliageLayers) return false;
    if (terrain.foliageLayers.empty()) {
        terrain.foliageMapWidth = kb::assets::TerrainAsset::DefaultFoliageMapResolution;
        terrain.foliageMapHeight = kb::assets::TerrainAsset::DefaultFoliageMapResolution;
    }
    terrain.foliageLayers.push_back(layer);
    terrain.foliageDensity.resize(
        static_cast<std::size_t>(terrain.foliageMapWidth) * terrain.foliageMapHeight * terrain.foliageLayers.size(), 0U);
    return true;
}

bool RemoveTerrainFoliageLayer(
    kb::assets::TerrainAsset& terrain,
    std::uint32_t layerIndex) noexcept {
    if (layerIndex >= terrain.foliageLayers.size()) return false;
    const std::size_t planeSize = static_cast<std::size_t>(terrain.foliageMapWidth) * terrain.foliageMapHeight;
    terrain.foliageLayers.erase(terrain.foliageLayers.begin() + static_cast<std::ptrdiff_t>(layerIndex));
    terrain.foliageDensity.erase(
        terrain.foliageDensity.begin() + static_cast<std::ptrdiff_t>(planeSize * layerIndex),
        terrain.foliageDensity.begin() + static_cast<std::ptrdiff_t>(planeSize * (layerIndex + 1U)));
    if (terrain.foliageLayers.empty()) {
        terrain.foliageMapWidth = 0U;
        terrain.foliageMapHeight = 0U;
    }
    return true;
}

TerrainLayerPaintResult ApplyTerrainFoliagePaint(
    kb::assets::TerrainAsset& terrain,
    const TerrainFoliagePaintSettings& settings,
    const TerrainBrushStamp& stamp) noexcept {
    return ApplyTerrainFoliagePaintSegment(terrain, settings, stamp, stamp);
}

TerrainLayerPaintResult ApplyTerrainFoliagePaintSegment(
    kb::assets::TerrainAsset& terrain,
    const TerrainFoliagePaintSettings& settings,
    const TerrainBrushStamp& start,
    const TerrainBrushStamp& end) noexcept {
    TerrainLayerPaintResult result{};
    const std::size_t planeSize = static_cast<std::size_t>(terrain.foliageMapWidth) * terrain.foliageMapHeight;
    if (settings.layerIndex >= terrain.foliageLayers.size() || terrain.foliageMapWidth < 2U ||
        terrain.foliageMapHeight < 2U || terrain.foliageDensity.size() != planeSize * terrain.foliageLayers.size()) {
        return result;
    }
    std::uint8_t* plane = terrain.foliageDensity.data() + planeSize * settings.layerIndex;
    ForEachSweptBrushTexel(
        terrain, terrain.foliageMapWidth, terrain.foliageMapHeight,
        SweptBrush{ settings.shape, settings.radius, settings.opacity, settings.falloff, settings.noiseSeed },
        start, end,
        [&](std::uint32_t x, std::uint32_t y, float, float, float influence) {
            std::uint8_t& density = plane[static_cast<std::size_t>(y) * terrain.foliageMapWidth + x];
            const std::uint8_t before = density;
            switch (settings.mode) {
            case TerrainFoliagePaintMode::Add:
                density = static_cast<std::uint8_t>(std::lround(std::lerp(static_cast<float>(before), 255.0F, influence)));
                break;
            case TerrainFoliagePaintMode::Erase:
                density = static_cast<std::uint8_t>(std::lround(std::lerp(static_cast<float>(before), 0.0F, influence)));
                break;
            case TerrainFoliagePaintMode::Remove:
                density = 0U;
                break;
            }
            if (density != before) MarkChanged(result, x, y);
        });
    return result;
}

} // namespace kb::terrain_editor
