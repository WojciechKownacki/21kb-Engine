#include "scene/prefab/ScenePrefabComponentSnapshot.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneObject.hpp"

namespace kb::scene {

ScenePrefabNodeComponents ScenePrefabComponentSnapshot::Capture(Scene& scene, SceneObject object) {
    ScenePrefabNodeComponents components;
    const SceneEntity entity = object.Entity();
    SceneComponents sceneComponents = scene.Components();

    if (const CameraComponent* camera = sceneComponents.Cameras().TryGet(entity)) {
        components.camera = *camera;
    }

    if (const MeshRendererComponent* meshRenderer = sceneComponents.MeshRenderers().TryGet(entity)) {
        components.meshRenderer = *meshRenderer;
    }

    if (const LightComponent* light = sceneComponents.Lights().TryGet(entity)) {
        components.light = *light;
    }

    return components;
}

} // namespace kb::scene
