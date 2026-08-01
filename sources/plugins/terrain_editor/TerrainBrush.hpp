#pragma once

#include "engine/assets/TerrainAsset.hpp"

#include <cstdint>

namespace kb::terrain_editor {

enum class TerrainBrushMode : std::uint8_t {
    Raise,
    Lower,
    Smooth,
    Flatten,
    Noise,
    Terrace,
    CutHole,
    FillHole,
};

enum class TerrainBrushShape : std::uint8_t {
    SoftRound,
    HardRound,
    LinearRound,
    Bell,
    Ring,
    Speckle,
};

struct TerrainBrushSettings {
    TerrainBrushMode mode = TerrainBrushMode::Raise;
    TerrainBrushShape shape = TerrainBrushShape::SoftRound;
    float radius = 8.0F;
    float strength = 1.0F;
    float falloff = 0.65F;
    float targetHeight = 0.0F;
    float terraceStep = 2.0F;
    std::uint32_t noiseSeed = 1U;
};

struct TerrainBrushStamp {
    float localX = 0.0F;
    float localZ = 0.0F;
    float pressure = 1.0F;
};

struct TerrainBrushResult {
    std::uint32_t changedSamples = 0U;
    std::uint32_t minX = UINT32_MAX;
    std::uint32_t minZ = UINT32_MAX;
    std::uint32_t maxX = 0U;
    std::uint32_t maxZ = 0U;

    [[nodiscard]] constexpr bool Changed() const noexcept { return changedSamples != 0U; }
};

[[nodiscard]] bool IsTerrainBrushSettingsValid(const TerrainBrushSettings& settings) noexcept;
[[nodiscard]] TerrainBrushResult ApplyTerrainBrush(
    kb::assets::TerrainAsset& terrain,
    const TerrainBrushSettings& settings,
    const TerrainBrushStamp& stamp) noexcept;

} // namespace kb::terrain_editor
