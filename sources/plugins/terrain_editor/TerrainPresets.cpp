#include "TerrainPresets.hpp"

#include "TerrainErosion.hpp"
#include "TerrainGenerators.hpp"

#include <algorithm>
#include <array>

namespace kb::terrain_editor {
namespace {

constexpr std::array<TerrainPresetInfo, 5> kPresets{ {
    { "flat", "level ground at height 0" },
    { "hills", "low rolling hills, lightly weathered" },
    { "mountains", "ranges with sharp crests, cut by rivers and slumped to their talus" },
    { "island", "an island in the sea, eroded" },
    { "plateau", "flat-topped mesas with cliffs" },
} };

struct Recipe {
    TerrainGeneratorSettings generator;
    TerrainHydraulicSettings hydraulic;
    bool hydraulicUsed = false;
    TerrainThermalSettings thermal;
    bool thermalUsed = false;
};

[[nodiscard]] Recipe MakeRecipe(std::string_view name, float size, std::uint32_t seed) {
    Recipe recipe;
    recipe.generator.seed = seed;
    if (name == "hills") {
        recipe.generator = { .kind = TerrainGeneratorKind::Fbm, .seed = seed, .amplitude = 0.07F * size, .scale = size / 3.0F, .octaves = 5U };
        recipe.hydraulic = { .iterations = 60U, .strength = 0.3F };
        recipe.hydraulicUsed = true;
        recipe.thermal = { .iterations = 10U, .talusDegrees = 40.0F };
        recipe.thermalUsed = true;
    } else if (name == "mountains") {
        recipe.generator = { .kind = TerrainGeneratorKind::Ridged, .seed = seed, .amplitude = 0.24F * size, .scale = size / 2.2F, .octaves = 6U,
            .warp = 0.04F * size };
        recipe.hydraulic = { .iterations = 140U, .strength = 0.4F };
        recipe.hydraulicUsed = true;
        recipe.thermal = { .iterations = 25U, .talusDegrees = 36.0F };
        recipe.thermalUsed = true;
    } else if (name == "island") {
        recipe.generator = { .kind = TerrainGeneratorKind::Island, .seed = seed, .amplitude = 0.18F * size, .scale = size / 3.0F, .octaves = 6U,
            .warp = 0.03F * size };
        recipe.hydraulic = { .iterations = 80U, .strength = 0.3F };
        recipe.hydraulicUsed = true;
        recipe.thermal = { .iterations = 12U, .talusDegrees = 38.0F };
        recipe.thermalUsed = true;
    } else if (name == "plateau") {
        recipe.generator = { .kind = TerrainGeneratorKind::Plateau, .seed = seed, .amplitude = 0.16F * size, .scale = size / 2.5F, .octaves = 5U };
        recipe.thermal = { .iterations = 20U, .talusDegrees = 50.0F };
        recipe.thermalUsed = true;
    }
    return recipe;
}

} // namespace

std::span<const TerrainPresetInfo> TerrainPresets() noexcept {
    return kPresets;
}

bool IsTerrainPreset(std::string_view name) noexcept {
    return std::ranges::any_of(kPresets, [name](const TerrainPresetInfo& preset) { return preset.name == name; });
}

bool ApplyTerrainPreset(
    kb::assets::TerrainAsset& terrain, std::string_view name, std::uint32_t seed, TerrainTaskControl& control, std::string& error) {
    if (!IsTerrainPreset(name)) {
        error = "unknown preset '" + std::string{ name } + "'";
        for (const TerrainPresetInfo& preset : kPresets) error += (error.find('(') == std::string::npos ? " (" : " ") + std::string{ preset.name };
        error += ')';
        return false;
    }
    if (name == "flat") return kb::assets::IsTerrainAssetValid(terrain, &error);
    const Recipe recipe = MakeRecipe(name, std::max(terrain.worldSizeX, terrain.worldSizeZ), seed);
    if (!GenerateTerrain(terrain, recipe.generator, control, error)) return false;
    if (recipe.hydraulicUsed && !ErodeTerrainHydraulic(terrain, recipe.hydraulic, control, error)) return false;
    if (recipe.thermalUsed && !ErodeTerrainThermal(terrain, recipe.thermal, control, error)) return false;
    return true;
}

} // namespace kb::terrain_editor
