#include "engine/scene/ScenePrefabs.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabAssetFacade.hpp"

#include <utility>

namespace kb::scene {

ScenePrefabHandle ScenePrefabs::CreateAsset(SceneObject root, std::string name, const std::filesystem::path& path) {
    return CreateAsset(root, ScenePrefabCaptureSettings{}, std::move(name), path);
}

ScenePrefabHandle ScenePrefabs::CreateAsset(SceneObject root, const ScenePrefabCaptureSettings& settings, std::string name, const std::filesystem::path& path) {
    return ScenePrefabAssetFacade::Create(scene_, root, settings, std::move(name), path);
}

bool ScenePrefabs::Save(ScenePrefabHandle handle, const std::filesystem::path& path) const {
    return ScenePrefabAssetFacade::Save(scene_, handle, path);
}

ScenePrefabHandle ScenePrefabs::Load(const std::filesystem::path& path) {
    return ScenePrefabAssetFacade::Load(scene_, path);
}

bool ScenePrefabs::Unload(ScenePrefabHandle handle) noexcept {
    return SceneAccess::State(scene_).prefabs.Remove(handle);
}

} // namespace kb::scene
