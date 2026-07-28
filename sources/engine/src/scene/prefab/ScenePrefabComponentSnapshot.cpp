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

    if (const InputComponent* input = sceneComponents.Inputs().TryGet(entity)) {
        components.input = *input;
    }

    if (const RigidbodyComponent* rigidbody = sceneComponents.Rigidbodies().TryGet(entity)) {
        components.rigidbody = *rigidbody;
    }

    if (const ColliderComponent* collider = sceneComponents.Colliders().TryGet(entity)) {
        components.collider = *collider;
    }

    if (const CharacterControllerComponent* characterController = sceneComponents.CharacterControllers().TryGet(entity)) {
        components.characterController = *characterController;
    }

    if (const JointComponent* joint = sceneComponents.Joints().TryGet(entity)) {
        // CaptureService resolves this transient entity id to a stable prefab-node
        // id before the prefab can leave capture. It is never serialized.
        components.joint = ScenePrefabJointComponent{
            .type = joint->type,
            .connectedNodeStableId = joint->connectedEntity.Id(),
            .anchor = joint->anchor,
            .connectedAnchor = joint->connectedAnchor,
            .axis = joint->axis,
            .minLimit = joint->minLimit,
            .maxLimit = joint->maxLimit,
            .enableLimit = joint->enableLimit,
        };
    }

    if (const TagsComponent* tags = sceneComponents.Tags().TryGet(entity)) {
        components.tags = *tags;
    }

    if (const BehaviourComponent* behaviour = sceneComponents.Behaviours().TryGet(entity)) {
        components.behaviour = *behaviour;
    }

    if (const AudioSourceComponent* audioSource = sceneComponents.AudioSources().TryGet(entity)) {
        components.audioSource = *audioSource;
    }

    if (const AudioListenerComponent* audioListener = sceneComponents.AudioListeners().TryGet(entity)) {
        components.audioListener = *audioListener;
    }
    if (const Animator* animator = sceneComponents.Animators().TryGet(entity)) {
        components.animator = *animator;
    }
    if (const UIDocumentComponent* uiDocument = sceneComponents.UIDocuments().TryGet(entity)) {
        components.uiDocument = *uiDocument;
    }

    return components;
}

} // namespace kb::scene
