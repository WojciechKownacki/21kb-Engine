#include "scene/prefab/ScenePrefabComponentApplier.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"

namespace kb::scene {

void ScenePrefabComponentApplier::Apply(Scene& scene, SceneObject object, const ScenePrefabNodeComponents& components) {
    SceneComponents sceneComponents = scene.Components();
    const SceneEntity entity = object.Entity();

    if (components.camera.has_value()) {
        sceneComponents.Cameras().Set(entity, *components.camera);
    }
    if (components.meshRenderer.has_value()) {
        sceneComponents.MeshRenderers().Set(entity, *components.meshRenderer);
    }
    if (components.light.has_value()) {
        sceneComponents.Lights().Set(entity, *components.light);
    }
}

} // namespace kb::scene
