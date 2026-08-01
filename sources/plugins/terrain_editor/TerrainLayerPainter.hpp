#pragma once

#include "TerrainBrush.hpp"
#include "engine/assets/TerrainAsset.hpp"

#include <cstdint>

namespace kb::terrain_editor {

struct TerrainLayerPaintSettings {
    TerrainBrushShape shape = TerrainBrushShape::SoftRound;
    std::uint32_t layerIndex = 0U;
    float radius = 8.0F;
    float opacity = 1.0F;
    float falloff = 0.65F;
    std::uint32_t noiseSeed = 1U;
    bool erase = false;
};

struct TerrainLayerPaintResult {
    std::uint32_t changedTexels = 0U;
    std::uint32_t minX = UINT32_MAX;
    std::uint32_t minY = UINT32_MAX;
    std::uint32_t maxX = 0U;
    std::uint32_t maxY = 0U;

    [[nodiscard]] constexpr bool Changed() const noexcept { return changedTexels != 0U; }
};

[[nodiscard]] bool AddTerrainMaterialLayer(
    kb::assets::TerrainAsset& terrain,
    std::uint64_t materialAssetId) noexcept;
[[nodiscard]] bool RemoveTerrainMaterialLayer(
    kb::assets::TerrainAsset& terrain,
    std::uint32_t layerIndex) noexcept;
[[nodiscard]] bool SetTerrainMaterialLayer(
    kb::assets::TerrainAsset& terrain,
    std::uint32_t layerIndex,
    std::uint64_t materialAssetId) noexcept;
[[nodiscard]] TerrainLayerPaintResult ApplyTerrainLayerPaint(
    kb::assets::TerrainAsset& terrain,
    const TerrainLayerPaintSettings& settings,
    const TerrainBrushStamp& stamp) noexcept;

} // namespace kb::terrain_editor
