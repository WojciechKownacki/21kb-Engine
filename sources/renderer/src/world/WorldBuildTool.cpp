#include "kb/render/world/WorldBuildTool.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/render/runtime/RuntimeRenderAssetDiscovery.hpp"
#include "kb/render/world/WorldHlodMeshBaker.hpp"

namespace kb::render {

kb::world::WorldBuildResult BuildWorldWithHlod(kb::assets::AssetManager& assets, const std::filesystem::path& descriptorPath) {
    WorldHlodMeshBaker baker{ assets };
    return kb::world::WorldCellBuilder::Build(descriptorPath, &baker);
}

kb::world::WorldBuildResult BuildAllWorldsWithHlod(
    kb::assets::AssetManager& assets, const std::filesystem::path& root, std::size_t& builtWorlds) {
    WorldHlodMeshBaker baker{ assets };
    return kb::world::WorldCellBuilder::BuildAll(root, &baker, builtWorlds);
}

kb::world::WorldBuildResult BuildWorldFromContentRoot(const std::filesystem::path& contentRoot, const std::filesystem::path& descriptorPath) {
    // The cooker's loader set: no modules, no simulation, the runtime render loaders.
    kb::scene::Scene scene{ kb::scene::SceneMode::PrefabPrivate };
    RuntimeRenderAssetDiscovery discovery;
    discovery.SetDiscoveryEnabled(false);
    discovery.Ensure(scene, 0U);
    kb::assets::AssetManager& assets = scene.Assets().Manager();
    if (!assets.Mounts().Mount("Game", contentRoot)) {
        return { .succeeded = false, .report = {}, .error = "could not mount the content root " + contentRoot.generic_string() };
    }
    static_cast<void>(assets.DiscoverMountedAssets());
    return BuildWorldWithHlod(assets, descriptorPath);
}

} // namespace kb::render
