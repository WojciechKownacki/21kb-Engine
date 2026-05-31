#include "engine/scene/SceneAssets.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

namespace kb::scene {

SceneAssets::SceneAssets(Scene& scene) noexcept
    : scene_(scene) {}

kb::assets::AssetManager& SceneAssets::Manager() noexcept {
    return SceneAccess::State(scene_).assets;
}

const kb::assets::AssetManager& SceneAssets::Manager() const noexcept {
    return SceneAccess::State(scene_).assets;
}

bool SceneAssets::MountProject(const std::filesystem::path& projectRoot) {
    return Manager().Mounts().Mount("Game", projectRoot / "Assets");
}

std::size_t SceneAssets::Discover() {
    return Manager().DiscoverMountedAssets();
}

kb::assets::AssetHandle<ScenePrefab> SceneAssets::LoadPrefab(const std::filesystem::path& virtualPath) {
    return Manager().Load<ScenePrefab>(virtualPath);
}

} // namespace kb::scene
