#pragma once

#include "engine/navigation/NavGeometryCollector.hpp"

#include <filesystem>

namespace kb::assets {
class AssetManager;
}

namespace kb::render {

// The scene navigation bake every tool runs: static meshes, terrain and colliders of the scene,
// with the meshes resolved through `assets` (which must hold the renderer's mesh loaders). The
// editor passes its scene's asset manager; kb_cli mounts the project's content root as /Game
// the same way, so a scene bakes to identical bytes in both.
[[nodiscard]] kb::navigation::NavSceneBakeResult BakeSceneNavMeshWithAssets(kb::assets::AssetManager& assets,
    const std::filesystem::path& scenePath, const kb::navigation::NavMeshBuildSettings& settings);

// For a tool without a scene of its own (kb_cli): mounts `contentRoot` as /Game with the
// cooker's runtime mesh loaders, discovers it, and bakes the scene.
[[nodiscard]] kb::navigation::NavSceneBakeResult BakeSceneNavMeshFromContentRoot(const std::filesystem::path& contentRoot,
    const std::filesystem::path& scenePath, const kb::navigation::NavMeshBuildSettings& settings);

} // namespace kb::render
