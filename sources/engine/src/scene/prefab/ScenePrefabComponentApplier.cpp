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
    if (components.input.has_value()) {
        sceneComponents.Inputs().Set(entity, *components.input);
    }
    if (components.rigidbody.has_value()) {
        sceneComponents.Rigidbodies().Set(entity, *components.rigidbody);
    }
    if (components.collider.has_value()) {
        sceneComponents.Colliders().Set(entity, *components.collider);
    }
    if (components.characterController.has_value()) {
        sceneComponents.CharacterControllers().Set(entity, *components.characterController);
    }
    if (components.tags.has_value()) {
        sceneComponents.Tags().Set(entity, *components.tags);
    }
    if (components.regionShape.has_value()) {
        sceneComponents.RegionShapes().Set(entity, *components.regionShape);
    }
    if (components.guideCurve.has_value()) {
        sceneComponents.GuideCurves().Set(entity, *components.guideCurve);
    }
    if (components.contentInstance.has_value()) {
        sceneComponents.ContentInstances().Set(entity, *components.contentInstance);
    }
    if (components.streamFocus.has_value()) {
        sceneComponents.StreamFocuses().Set(entity, *components.streamFocus);
    }
    if (components.worldBackdrop.has_value()) {
        sceneComponents.WorldBackdrops().Set(entity, *components.worldBackdrop);
    }
    if (components.ambientRadiance.has_value()) {
        sceneComponents.AmbientRadiances().Set(entity, *components.ambientRadiance);
    }
    if (components.behaviour.has_value()) {
        sceneComponents.Behaviours().Set(entity, *components.behaviour);
    }
    if (components.audioSource.has_value()) {
        sceneComponents.AudioSources().Set(entity, *components.audioSource);
    }
    if (components.audioListener.has_value()) {
        sceneComponents.AudioListeners().Set(entity, *components.audioListener);
    }
    if (components.animator.has_value()) {
        sceneComponents.Animators().Set(entity, *components.animator);
    }
    if (components.uiDocument.has_value()) {
        sceneComponents.UIDocuments().Set(entity, *components.uiDocument);
    }
    if (components.navAgent.has_value()) {
        sceneComponents.NavAgents().Set(entity, *components.navAgent);
    }
    if (components.navObstacle.has_value()) {
        sceneComponents.NavObstacles().Set(entity, *components.navObstacle);
    }
}

} // namespace kb::scene
