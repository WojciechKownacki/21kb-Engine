#pragma once

#include "engine/world/WorldCellBuilder.hpp"

#include <cstddef>
#include <filesystem>

namespace kb::assets {
class AssetManager;
}

namespace kb::render {

// The one world build every tool runs: cells, cell index, HLOD proxy meshes and (when the world
// enables navigation) the cells' navigation meshes, with the meshes resolved through `assets`.
// The editor passes its scene's asset manager, kb_cooker the manager it cooks from; both mount
// the project's content root as /Game, so a world built by either produces byte-identical output.
[[nodiscard]] kb::world::WorldBuildResult BuildWorldWithHlod(
    kb::assets::AssetManager& assets, const std::filesystem::path& descriptorPath);
[[nodiscard]] kb::world::WorldBuildResult BuildAllWorldsWithHlod(
    kb::assets::AssetManager& assets, const std::filesystem::path& root, std::size_t& builtWorlds);

// For a tool without a scene of its own (kb_cli): mounts `contentRoot` as /Game with the
// same runtime mesh loaders kb_cooker installs, discovers it, and builds the world.
[[nodiscard]] kb::world::WorldBuildResult BuildWorldFromContentRoot(
    const std::filesystem::path& contentRoot, const std::filesystem::path& descriptorPath);

} // namespace kb::render
