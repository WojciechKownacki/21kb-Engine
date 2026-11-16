#include "engine/assets/TerrainAsset.hpp"

#include <cmath>
#include <limits>
#include <utility>

namespace kb::assets {
namespace {

void SetError(std::string* output, std::string value) {
    if (output != nullptr) *output = std::move(value);
}

[[nodiscard]] bool IsPowerOfTwo(std::uint32_t value) noexcept {
    return value != 0U && (value & (value - 1U)) == 0U;
}

} // namespace

bool IsTerrainResolutionValid(std::uint32_t value) noexcept {
    return value >= TerrainAsset::MinimumResolution && value <= TerrainAsset::MaximumResolution &&
        IsPowerOfTwo(value - 1U);
}

bool IsTerrainAssetValid(const TerrainAsset& terrain, std::string* error) noexcept {
    if (!IsTerrainResolutionValid(terrain.width) || !IsTerrainResolutionValid(terrain.height)) {
        SetError(error, "Terrain dimensions must be 2^n + 1 and within the supported range");
        return false;
    }
    if (!std::isfinite(terrain.worldSizeX) || !std::isfinite(terrain.worldSizeZ) ||
        terrain.worldSizeX <= 0.0F || terrain.worldSizeZ <= 0.0F) {
        SetError(error, "Terrain world size must be finite and positive");
        return false;
    }
    if (terrain.chunkQuads < 8U || terrain.chunkQuads > 128U || !IsPowerOfTwo(terrain.chunkQuads)) {
        SetError(error, "Terrain chunk size must be a power of two from 8 to 128 quads");
        return false;
    }
    if (terrain.lodCount == 0U || terrain.lodCount > 8U) {
        SetError(error, "Terrain LOD count must be from 1 to 8");
        return false;
    }
    const std::uint64_t vertexCount = static_cast<std::uint64_t>(terrain.width) * terrain.height;
    const std::uint64_t cellCount = static_cast<std::uint64_t>(terrain.width - 1U) * (terrain.height - 1U);
    if (terrain.heights.size() != vertexCount || terrain.holes.size() != cellCount) {
        SetError(error, "Terrain sample buffers do not match its dimensions");
        return false;
    }
    for (const float value : terrain.heights) {
        if (!std::isfinite(value) || std::abs(value) > 1'000'000.0F) {
            SetError(error, "Terrain contains an invalid height sample");
            return false;
        }
    }
    for (const std::uint8_t value : terrain.holes) {
        if (value > 1U) {
            SetError(error, "Terrain hole mask contains an invalid value");
            return false;
        }
    }
    if (terrain.materialLayers.size() > TerrainAsset::MaximumMaterialLayers) {
        SetError(error, "Terrain supports at most four material layers");
        return false;
    }
    if (terrain.materialLayers.empty()) {
        if (terrain.layerWeightWidth != 0U || terrain.layerWeightHeight != 0U || !terrain.layerWeights.empty()) {
            SetError(error, "Terrain without material layers must not contain a layer weight map");
            return false;
        }
    } else {
        if (terrain.layerWeightWidth == 0U || terrain.layerWeightHeight == 0U ||
            terrain.layerWeightWidth > TerrainAsset::MaximumResolution ||
            terrain.layerWeightHeight > TerrainAsset::MaximumResolution) {
            SetError(error, "Terrain layer weight map dimensions are invalid");
            return false;
        }
        const std::uint64_t weightBytes = static_cast<std::uint64_t>(terrain.layerWeightWidth) *
            terrain.layerWeightHeight * TerrainAsset::MaximumMaterialLayers;
        if (weightBytes != terrain.layerWeights.size()) {
            SetError(error, "Terrain layer weight map payload does not match its dimensions");
            return false;
        }
        const std::uint32_t layerCount = static_cast<std::uint32_t>(terrain.materialLayers.size());
        for (std::size_t texel = 0U; texel < terrain.layerWeights.size(); texel += TerrainAsset::MaximumMaterialLayers) {
            std::uint32_t sum = 0U;
            for (std::uint32_t layer = 0U; layer < TerrainAsset::MaximumMaterialLayers; ++layer) {
                const std::uint8_t weight = terrain.layerWeights[texel + layer];
                if (layer >= layerCount && weight != 0U) {
                    SetError(error, "Terrain layer weight map contains data for an inactive layer");
                    return false;
                }
                sum += weight;
            }
            if (sum != 255U) {
                SetError(error, "Terrain material layer weights must sum to 255 per texel");
                return false;
            }
        }
    }
    if (terrain.foliageLayers.size() > TerrainAsset::MaximumFoliageLayers) {
        SetError(error, "Terrain supports at most eight foliage layers");
        return false;
    }
    if (terrain.foliageLayers.empty()) {
        if (terrain.foliageMapWidth != 0U || terrain.foliageMapHeight != 0U || !terrain.foliageDensity.empty()) {
            SetError(error, "Terrain without foliage layers must not contain a foliage density map");
            return false;
        }
    } else {
        if (terrain.foliageMapWidth < 2U || terrain.foliageMapHeight < 2U ||
            terrain.foliageMapWidth > TerrainAsset::MaximumResolution ||
            terrain.foliageMapHeight > TerrainAsset::MaximumResolution ||
            static_cast<std::uint64_t>(terrain.foliageMapWidth) * terrain.foliageMapHeight *
                terrain.foliageLayers.size() != terrain.foliageDensity.size()) {
            SetError(error, "Terrain foliage density map does not match its dimensions");
            return false;
        }
        for (const TerrainFoliageLayer& layer : terrain.foliageLayers) {
            const bool finite = std::isfinite(layer.density) && std::isfinite(layer.minScale) &&
                std::isfinite(layer.maxScale) && std::isfinite(layer.rotationRangeDegrees) &&
                std::isfinite(layer.alignToNormal) && std::isfinite(layer.fadeStartDistance) &&
                std::isfinite(layer.cullDistance);
            if (!finite || layer.meshAssetId == 0U || layer.density < 0.01F || layer.density > 100.0F ||
                layer.minScale <= 0.0F || layer.maxScale < layer.minScale ||
                layer.rotationRangeDegrees < 0.0F || layer.rotationRangeDegrees > 360.0F ||
                layer.alignToNormal < 0.0F || layer.alignToNormal > 1.0F ||
                layer.fadeStartDistance < 0.0F || layer.cullDistance <= layer.fadeStartDistance) {
                SetError(error, "Terrain foliage layer has an invalid setting");
                return false;
            }
        }
    }
    if (error != nullptr) error->clear();
    return true;
}

TerrainAsset MakeFlatTerrainAsset(std::uint32_t resolution, float worldSizeX, float worldSizeZ) {
    TerrainAsset terrain{};
    terrain.width = resolution;
    terrain.height = resolution;
    terrain.worldSizeX = worldSizeX;
    terrain.worldSizeZ = worldSizeZ;
    if (!IsTerrainResolutionValid(resolution) || !std::isfinite(worldSizeX) || !std::isfinite(worldSizeZ) ||
        worldSizeX <= 0.0F || worldSizeZ <= 0.0F) {
        return {};
    }
    terrain.heights.assign(static_cast<std::size_t>(resolution) * resolution, 0.0F);
    terrain.holes.assign(static_cast<std::size_t>(resolution - 1U) * (resolution - 1U), 0U);
    return terrain;
}

} // namespace kb::assets
