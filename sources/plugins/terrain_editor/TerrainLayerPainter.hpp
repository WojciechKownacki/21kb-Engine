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
    // Choose the layer under every texel by its slope instead of using layerIndex (see kTerrainAutoRockSlopeDegrees).
    bool autoLayer = false;
};

// Auto painting uses two layers: ground flatter than this takes the first layer, steeper ground
// the last one (grass and rock). Layers between them are left to painting by hand.
inline constexpr float kTerrainAutoRockSlopeDegrees = 35.0F;

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

// The splat map is square and 512, 1024 or 2048 texels across.
[[nodiscard]] constexpr bool IsTerrainSplatResolution(std::uint32_t value) noexcept {
    return value >= kb::assets::TerrainAsset::MinimumLayerWeightResolution &&
        value <= kb::assets::TerrainAsset::MaximumLayerWeightResolution && (value & (value - 1U)) == 0U;
}

// The first layer of a terrain makes its splat map, `splatResolution` texels across and all of that layer
// (false for a resolution that is not one of IsTerrainSplatResolution). Later layers keep the map's size.
[[nodiscard]] bool AddTerrainMaterialLayer(
    kb::assets::TerrainAsset& terrain,
    std::uint64_t materialAssetId,
    std::uint32_t splatResolution = kb::assets::TerrainAsset::DefaultLayerWeightResolution) noexcept;
// Resamples the splat map of a terrain with layers to `resolution` (bilinear over the same world area, up or
// down); every texel still sums to 255 and the channels of absent layers stay 0. `maxWorkers` caps the
// threads (0: every core). False, and nothing changed, without layers or for another resolution.
[[nodiscard]] bool SetTerrainSplatResolution(
    kb::assets::TerrainAsset& terrain,
    std::uint32_t resolution,
    std::uint32_t maxWorkers = 0U) noexcept;
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
