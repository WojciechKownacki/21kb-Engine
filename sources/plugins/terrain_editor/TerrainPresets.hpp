#pragma once

#include "TerrainTask.hpp"
#include "engine/assets/TerrainAsset.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace kb::terrain_editor {

struct TerrainPresetInfo {
    std::string_view name;
    std::string_view description;
};

// The "new world" recipes: a generator sized to the terrain, then the erosion that finishes it.
[[nodiscard]] std::span<const TerrainPresetInfo> TerrainPresets() noexcept;
[[nodiscard]] bool IsTerrainPreset(std::string_view name) noexcept;

inline constexpr float kTerrainDefaultCharacter = 0.5F;

// Fills the terrain from a preset; the seed picks the world. Every preset is deterministic: the
// same name, seed, character and terrain size give the same heights. "flat" leaves the terrain as it is.
// `character` (0..1, refused outside it) shades the preset from gentle to dramatic: relief and the
// strength of the hydraulic erosion scale from 0.5x to 1.5x and the weight of fine detail from
// gain 0.4 to 0.6; kTerrainDefaultCharacter (0.5) is the preset as it is. A new world has no holes.
[[nodiscard]] bool ApplyTerrainPreset(
    kb::assets::TerrainAsset& terrain,
    std::string_view name,
    std::uint32_t seed,
    float character,
    TerrainTaskControl& control,
    std::string& error);

} // namespace kb::terrain_editor
