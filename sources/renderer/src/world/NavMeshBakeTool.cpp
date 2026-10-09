#include "kb/render/world/NavMeshBakeTool.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/render/runtime/RuntimeRenderAssetDiscovery.hpp"
#include "kb/render/world/RenderNavGeometrySource.hpp"

namespace kb::render {

kb::navigation::NavSceneBakeResult BakeSceneNavMeshWithAssets(kb::assets::AssetManager& assets, const std::filesystem::path& scenePath,
    const kb::navigation::NavMeshBuildSettings& settings) {
    RenderNavGeometrySource source{ assets };
    return kb::navigation::BakeSceneNavMesh(scenePath, settings, &source);
}

kb::navigation::NavSceneBakeResult BakeSceneNavMeshFromContentRoot(const std::filesystem::path& contentRoot, const std::filesystem::path& scenePath,
    const kb::navigation::NavMeshBuildSettings& settings) {
    // The cooker's loader set: no modules, no simulation, the runtime render loaders.
    kb::scene::Scene scene{ kb::scene::SceneMode::PrefabPrivate };
    RuntimeRenderAssetDiscovery discovery;
    discovery.SetDiscoveryEnabled(false);
    discovery.Ensure(scene, 0U);
    kb::assets::AssetManager& assets = scene.Assets().Manager();
    if (!assets.Mounts().Mount("Game", contentRoot)) {
        kb::navigation::NavSceneBakeResult failed;
        failed.error = "could not mount the content root " + contentRoot.generic_string();
        return failed;
    }
    static_cast<void>(assets.DiscoverMountedAssets());
    return BakeSceneNavMeshWithAssets(assets, scenePath, settings);
}

} // namespace kb::render
