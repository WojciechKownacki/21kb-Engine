#include "scene/EditorScenePrefabActions.hpp"

#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/ScenePrefabs.hpp"

namespace kb::editor {

bool EditorScenePrefabActions::CreateAsset(kb::scene::Scene& scene, kb::scene::SceneEntity entity, const std::filesystem::path& path) {
    kb::scene::SceneObject object = scene.Entities().Object(entity);
    if (!object.IsValid()) {
        return false;
    }

    const kb::scene::ScenePrefabHandle handle = scene.Prefabs().CreateAsset(object, scene.Entities().Name(entity), path);
    return handle.IsValid();
}

std::optional<kb::scene::SceneEntity> EditorScenePrefabActions::InstantiateAsset(kb::scene::Scene& scene, const std::filesystem::path& path, kb::scene::SceneEntity parent) {
    const kb::scene::ScenePrefabHandle handle = scene.Prefabs().Load(path);
    if (!handle.IsValid()) {
        return std::nullopt;
    }

    kb::scene::SceneObject parentObject = scene.Entities().Object(parent);
    const kb::scene::ScenePrefabInstance instance = scene.Prefabs().Instantiate(
        handle,
        kb::scene::ScenePrefabInstantiationSettings{ .parent = parentObject });
    if (instance.Empty()) {
        return std::nullopt;
    }
    return instance.RootObject().Entity();
}

} // namespace kb::editor
