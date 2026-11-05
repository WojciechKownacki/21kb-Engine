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

// Paints a layer where the terrain is in a slope and height range (the way grass is kept to flat
// ground and rock to cliffs). The range edges fade over the blend widths.
struct TerrainAutoPaintSettings {
    std::uint32_t layerIndex = 0U;
    float minSlopeDegrees = 0.0F;
    float maxSlopeDegrees = 90.0F;
    float minHeight = -1'000'000.0F;
    float maxHeight = 1'000'000.0F;
    float slopeBlendDegrees = 3.0F;
    float heightBlend = 1.0F;
    float opacity = 1.0F;
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
[[nodiscard]] TerrainLayerPaintResult ApplyTerrainAutoPaint(
    kb::assets::TerrainAsset& terrain,
    const TerrainAutoPaintSettings& settings) noexcept;
[[nodiscard]] TerrainLayerPaintResult ApplyTerrainLayerPaint(
    kb::assets::TerrainAsset& terrain,
    const TerrainLayerPaintSettings& settings,
    const TerrainBrushStamp& stamp) noexcept;
[[nodiscard]] TerrainLayerPaintResult ApplyTerrainLayerPaintSegment(
    kb::assets::TerrainAsset& terrain,
    const TerrainLayerPaintSettings& settings,
    const TerrainBrushStamp& start,
    const TerrainBrushStamp& end) noexcept;

} // namespace kb::terrain_editor
