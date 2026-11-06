#pragma once

#include "TerrainNoise.hpp"
#include "TerrainTask.hpp"
#include "engine/assets/TerrainAsset.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace kb::terrain_editor {

enum class TerrainGeneratorKind : std::uint8_t {
    Fbm,     // rolling hills: fractal noise
    Ridged,  // mountain ranges with sharp crests
    Island,  // land that falls to the sea at the edges
    Plateau, // flat-topped mesas with cliffs
    Valley,  // a flat floor along z between walls that rise to the sides
};

// A whole heightmap from noise. Heights are baseHeight + amplitude x a 0..1 shape (fBm lies around
// the middle of it), so `amplitude` is the relief. Every height depends only on its own position
// and the seed: the same settings give the same terrain on any number of threads.
struct TerrainGeneratorSettings {
    TerrainGeneratorKind kind = TerrainGeneratorKind::Fbm;
    std::uint32_t seed = 1U;
    float amplitude = 24.0F;
    float baseHeight = 0.0F;
    // World units of the largest feature.
    float scale = 64.0F;
    std::uint8_t octaves = 6U;
    float lacunarity = 2.0F;
    float gain = 0.5F;
    TerrainNoiseKind noiseKind = TerrainNoiseKind::Gradient;
    // World units the noise lookup is pushed around by a second noise: twisted, less regular forms.
    float warp = 0.0F;
    // Island: where the coast begins, as a share of the half size, and how wide it falls to the sea.
    float islandRadius = 0.65F;
    float islandFalloff = 0.3F;
    // Plateau: the noise level above which mesas stand, and how soft their cliffs are.
    float plateauLevel = 0.55F;
    float plateauSoftness = 0.04F;
    // Valley: the floor's half width as a share of the half size, and how wide the walls take to rise.
    float valleyWidth = 0.15F;
    float valleyWall = 0.7F;
    // Add to the existing heights instead of replacing them.
    bool additive = false;
};

[[nodiscard]] std::string_view TerrainGeneratorKindName(TerrainGeneratorKind kind) noexcept;
[[nodiscard]] std::optional<TerrainGeneratorKind> ParseTerrainGeneratorKind(std::string_view text) noexcept;
[[nodiscard]] bool IsTerrainGeneratorValid(const TerrainGeneratorSettings& settings) noexcept;

// Fills the terrain's heights; false with a reason when the settings are wrong or the job was
// cancelled (then the heights are partly new - run it on a copy, as TerrainBackgroundTask does).
[[nodiscard]] bool GenerateTerrain(
    kb::assets::TerrainAsset& terrain,
    const TerrainGeneratorSettings& settings,
    TerrainTaskControl& control,
    std::string& error);

} // namespace kb::terrain_editor
