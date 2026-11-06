#pragma once

#include <filesystem>
#include <string>

namespace kb::terrain_editor {

// What a new river is drawn with: the materials of the stream and of the waterfalls, and the particle effect
// (RiverMist.kbvfx, with its material RiverMistMaterial.kbmat) of the mist at the foot of a waterfall.
struct RiverWaterMaterials {
    std::filesystem::path stream;
    std::filesystem::path waterfall;
    std::filesystem::path mist;
};

// Writes these into `folder` (made when missing); a file that is there is kept as it is, so a river can use
// versions the project edited. `files` names the three the river refers to.
[[nodiscard]] bool EnsureRiverWaterMaterials(const std::filesystem::path& folder, RiverWaterMaterials& files, std::string& error);

} // namespace kb::terrain_editor
