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

// Fills the terrain from a preset; the seed picks the world. Every preset is deterministic: the
// same name, seed and terrain size give the same heights. "flat" leaves the terrain as it is.
[[nodiscard]] bool ApplyTerrainPreset(
    kb::assets::TerrainAsset& terrain,
    std::string_view name,
    std::uint32_t seed,
    TerrainTaskControl& control,
    std::string& error);

} // namespace kb::terrain_editor
