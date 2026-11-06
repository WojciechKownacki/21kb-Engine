#pragma once

#include "TerrainTask.hpp"
#include "engine/assets/TerrainAsset.hpp"

#include <cstdint>
#include <string>

namespace kb::terrain_editor {

// Thermal erosion: slopes steeper than the talus angle slump downhill until they rest at it.
struct TerrainThermalSettings {
    std::uint32_t iterations = 30U;
    float talusDegrees = 35.0F;
    // The share of the excess that moves each iteration, 0..1.
    float strength = 0.5F;
};

// Hydraulic erosion on a grid (rain, water flowing between neighbouring cells, sediment carried by
// the flow, cut where it is fast and steep and laid down where it slows), so channels and fans
// grow where water would run. No random numbers: the result is the same on any number of threads.
struct TerrainHydraulicSettings {
    std::uint32_t iterations = 150U;
    // Rain per iteration as a share of the terrain's height range.
    float rain = 0.0015F;
    // The share of the water that evaporates each iteration.
    float evaporation = 0.03F;
    // Scales how much sediment the water carries: 0.4 cuts gentle channels, 1 deep gullies.
    float strength = 0.4F;
};

inline constexpr std::uint32_t kTerrainMaximumErosionIterations = 100'000U;

[[nodiscard]] bool IsTerrainThermalValid(const TerrainThermalSettings& settings) noexcept;
[[nodiscard]] bool IsTerrainHydraulicValid(const TerrainHydraulicSettings& settings) noexcept;

// Both change the heights in place; on a cancelled job the heights are partly eroded (run it on a
// copy, as TerrainBackgroundTask does). False with the reason otherwise.
[[nodiscard]] bool ErodeTerrainThermal(
    kb::assets::TerrainAsset& terrain, const TerrainThermalSettings& settings, TerrainTaskControl& control, std::string& error);
[[nodiscard]] bool ErodeTerrainHydraulic(
    kb::assets::TerrainAsset& terrain, const TerrainHydraulicSettings& settings, TerrainTaskControl& control, std::string& error);

} // namespace kb::terrain_editor
