#include "TerrainLayerPainter.hpp"

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

TerrainLayerPaintResult ApplyTerrainLayerPaint(
    kb::assets::TerrainAsset& terrain,
    const TerrainLayerPaintSettings& settings,
    const TerrainBrushStamp& stamp) noexcept {
    TerrainLayerPaintResult result{};
    const std::uint32_t layerCount = static_cast<std::uint32_t>(terrain.materialLayers.size());
    if (settings.layerIndex >= layerCount || terrain.layerWeightWidth == 0U || terrain.layerWeightHeight == 0U ||
        terrain.layerWeights.size() != static_cast<std::size_t>(terrain.layerWeightWidth) * terrain.layerWeightHeight * kChannels ||
        !std::isfinite(settings.radius) || settings.radius <= 0.0F ||
        !std::isfinite(settings.opacity) || settings.opacity < 0.0F ||
        !std::isfinite(settings.falloff) || settings.falloff < 0.0F || settings.falloff > 1.0F ||
        !std::isfinite(stamp.localX) || !std::isfinite(stamp.localZ) ||
        !std::isfinite(stamp.pressure) || stamp.pressure < 0.0F) {
        return result;
    }
    if (settings.erase && layerCount == 1U) return result;
    const float texelSizeX = terrain.worldSizeX / static_cast<float>(terrain.layerWeightWidth - 1U);
    const float texelSizeY = terrain.worldSizeZ / static_cast<float>(terrain.layerWeightHeight - 1U);
    const int centerX = static_cast<int>(std::lround(
        (stamp.localX + terrain.worldSizeX * 0.5F) / texelSizeX));
    const int centerY = static_cast<int>(std::lround(
        (stamp.localZ + terrain.worldSizeZ * 0.5F) / texelSizeY));
    const int extentX = static_cast<int>(std::ceil(settings.radius / texelSizeX));
    const int extentY = static_cast<int>(std::ceil(settings.radius / texelSizeY));
    const int minX = std::max(centerX - extentX, 0);
    const int minY = std::max(centerY - extentY, 0);
    const int maxX = std::min(centerX + extentX, static_cast<int>(terrain.layerWeightWidth) - 1);
    const int maxY = std::min(centerY + extentY, static_cast<int>(terrain.layerWeightHeight) - 1);
    const float radiusSquared = settings.radius * settings.radius;
    const TerrainBrushSettings brush{
        .shape = settings.shape,
        .radius = settings.radius,
        .strength = 1.0F,
        .falloff = settings.falloff,
        .noiseSeed = settings.noiseSeed,
    };
    const float opacity = std::clamp(settings.opacity * stamp.pressure, 0.0F, 1.0F);
    for (int y = minY; y <= maxY; ++y) {
        const float worldZ = static_cast<float>(y) * texelSizeY - terrain.worldSizeZ * 0.5F;
        for (int x = minX; x <= maxX; ++x) {
            const float worldX = static_cast<float>(x) * texelSizeX - terrain.worldSizeX * 0.5F;
            const float dx = worldX - stamp.localX;
            const float dz = worldZ - stamp.localZ;
            const float distanceSquared = dx * dx + dz * dz;
            if (distanceSquared > radiusSquared) continue;
            const float influence = TerrainBrushWeight(
                std::sqrt(distanceSquared), static_cast<std::uint32_t>(x),
                static_cast<std::uint32_t>(y), brush) * opacity;
            if (influence <= 0.0F) continue;
            std::uint8_t* weights = terrain.layerWeights.data() +
                (static_cast<std::size_t>(y) * terrain.layerWeightWidth + static_cast<std::uint32_t>(x)) * kChannels;
            const std::uint32_t before = weights[settings.layerIndex];
            const float target = settings.erase ? 0.0F : 255.0F;
            const std::uint32_t after = static_cast<std::uint32_t>(std::lround(
                std::lerp(static_cast<float>(before), target, influence)));
            if (after == before) continue;
            NormalizeAfterTargetChange(weights, layerCount, settings.layerIndex, after);
            MarkChanged(result, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
        }
    }
    return result;
}

} // namespace kb::terrain_editor
