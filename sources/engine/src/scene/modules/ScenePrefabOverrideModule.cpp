#include "engine/scene/ScenePrefabs.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabOverrideFacade.hpp"

#include <utility>

namespace kb::scene {

bool ScenePrefabs::IsInstance(ScenePrefabInstanceHandle handle) const noexcept {
    return ScenePrefabOverrideFacade::IsInstance(scene_, handle);
}

ScenePrefabInstanceHandle ScenePrefabs::RootInstance(SceneObject object) const noexcept {
    return SceneAccess::State(scene_).prefabInstances.FindRootInstance(object);
}

ScenePrefabInstanceHandle ScenePrefabs::RootInstance(SceneEntity entity) const noexcept {
    return RootInstance(SceneAccess::MakeObject(scene_, entity));
}

ScenePrefabInstanceHandle ScenePrefabs::ContainingInstance(SceneObject object, std::uint32_t& nodeIndex) const noexcept {
    return SceneAccess::State(scene_).prefabInstances.FindContainingInstance(object, nodeIndex);
}

ScenePrefabInstanceHandle ScenePrefabs::ContainingInstance(SceneEntity entity, std::uint32_t& nodeIndex) const noexcept {
    return ContainingInstance(SceneAccess::MakeObject(scene_, entity), nodeIndex);
}

ScenePrefabOverrideReport ScenePrefabs::Overrides(ScenePrefabInstanceHandle handle) const {
    return ScenePrefabOverrideFacade::Overrides(scene_, handle);
}

bool ScenePrefabs::RevertOverrides(ScenePrefabInstanceHandle handle) {
    return ScenePrefabOverrideFacade::Revert(scene_, handle);
}

bool ScenePrefabs::ApplyOverrides(ScenePrefabInstanceHandle handle) {
    return ScenePrefabOverrideFacade::Apply(scene_, handle);
}

bool ScenePrefabs::ApplyOverrides(ScenePrefabInstanceHandle handle, const std::filesystem::path& assetPath) {
    return ScenePrefabOverrideFacade::ApplyAndSave(scene_, handle, assetPath);
}

bool ScenePrefabs::RevertOverride(ScenePrefabInstanceHandle handle, std::uint32_t nodeIndex, std::string propertyPath) {
    return ScenePrefabOverrideFacade::RevertProperty(scene_, handle, nodeIndex, std::move(propertyPath));
}

bool ScenePrefabs::ApplyOverride(ScenePrefabInstanceHandle handle, std::uint32_t nodeIndex, std::string propertyPath) {
    return ScenePrefabOverrideFacade::ApplyProperty(scene_, handle, nodeIndex, std::move(propertyPath));
}

bool ScenePrefabs::ApplyOverride(ScenePrefabInstanceHandle handle, std::uint32_t nodeIndex, std::string propertyPath, const std::filesystem::path& assetPath) {
    return ScenePrefabOverrideFacade::ApplyPropertyAndSave(scene_, handle, nodeIndex, std::move(propertyPath), assetPath);
}

} // namespace kb::scene
