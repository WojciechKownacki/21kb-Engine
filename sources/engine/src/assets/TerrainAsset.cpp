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
