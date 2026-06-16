#include "engine/scene/ScenePrefabs.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabOverrideFacade.hpp"

#include <utility>

namespace kb::scene {
namespace {

[[nodiscard]] bool IsLiveSceneObject(Scene& scene, SceneObject object) noexcept {
    return SceneAccess::BelongsTo(scene, object) && scene.Entities().IsAlive(object);
}

} // namespace

bool ScenePrefabs::IsInstance(ScenePrefabInstanceHandle handle) const noexcept {
    return ScenePrefabOverrideFacade::IsInstance(scene_, handle);
}

ScenePrefabInstanceHandle ScenePrefabs::RootInstance(SceneObject object) const noexcept {
    if (!IsLiveSceneObject(scene_, object)) {
        return {};
    }
    return SceneAccess::State(scene_).prefabInstances.FindRootInstance(object);
}

ScenePrefabInstanceHandle ScenePrefabs::RootInstance(SceneEntity entity) const noexcept {
    return RootInstance(SceneAccess::MakeObject(scene_, entity));
}

ScenePrefabInstanceHandle ScenePrefabs::ContainingInstance(SceneObject object, std::uint32_t& nodeIndex) const noexcept {
    nodeIndex = 0;
    if (!IsLiveSceneObject(scene_, object)) {
        return {};
    }
    return SceneAccess::State(scene_).prefabInstances.FindContainingInstance(object, nodeIndex);
}

ScenePrefabInstanceHandle ScenePrefabs::ContainingInstance(SceneEntity entity, std::uint32_t& nodeIndex) const noexcept {
    return ContainingInstance(SceneAccess::MakeObject(scene_, entity), nodeIndex);
}

ScenePrefabInstanceHandle ScenePrefabs::ContainingInstance(SceneObject object, std::uint32_t& nodeIndex, std::uint64_t& nodeId) const noexcept {
    nodeIndex = 0;
    nodeId = ScenePrefabNodeDesc::InvalidStableId;
    if (!IsLiveSceneObject(scene_, object)) {
        return {};
    }
    return SceneAccess::State(scene_).prefabInstances.FindContainingInstance(object, nodeIndex, nodeId);
}

ScenePrefabInstanceHandle ScenePrefabs::ContainingInstance(SceneEntity entity, std::uint32_t& nodeIndex, std::uint64_t& nodeId) const noexcept {
    return ContainingInstance(SceneAccess::MakeObject(scene_, entity), nodeIndex, nodeId);
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
