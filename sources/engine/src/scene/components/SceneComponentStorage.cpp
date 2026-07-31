#include "scene/components/SceneComponentStorage.hpp"

#include "scene/components/SceneComponentRegistry.hpp"

namespace kb::scene {

SceneComponentStorage::SceneComponentStorage(kb::ecs::World& world, const SceneComponentRegistry& components) noexcept
    : transforms_(world, components.TransformComponentId())
    , visibility_(world, components.VisibilityComponentId())
    , behaviours_(world, components.BehaviourComponentId())
    , cameras_(world, components.CameraComponentId())
    , meshRenderers_(world, components.MeshRendererComponentId())
    , lights_(world, components.LightComponentId())
    , inputs_(world, components.InputComponentId())
    , rigidbodies_(world, components.RigidbodyComponentId())
    , colliders_(world, components.ColliderComponentId())
    , characterControllers_(world, components.CharacterControllerComponentId())
    , joints_(world, components.JointComponentId())
    , tags_(world, components.TagsComponentId())
    , regionShapes_(world, components.RegionShapeComponentId())
    , guideCurves_(world, components.GuideCurveComponentId())
    , contentInstances_(world, components.ContentInstanceComponentId())
    , streamFocuses_(world, components.StreamFocusComponentId())
    , worldBackdrops_(world, components.WorldBackdropComponentId())
    , audioSources_(world, components.AudioSourceComponentId())
    , audioListeners_(world, components.AudioListenerComponentId())
    , animators_(world, components.AnimatorComponentId())
    , uiDocuments_(world, components.UIDocumentComponentId())
    , navigation_(world) {}

void SceneComponentStorage::SetDefaults(SceneEntity entity, const TransformComponent& transform, const VisibilityComponent& visibility) {
    transforms_.Set(entity, transform);
    visibility_.Set(entity, visibility);
}

const SceneTransformComponentStore& SceneComponentStorage::Transforms() const noexcept {
    return transforms_;
}

SceneTransformComponentStore& SceneComponentStorage::Transforms() noexcept {
    return transforms_;
}

const SceneVisibilityComponentStore& SceneComponentStorage::Visibility() const noexcept {
    return visibility_;
}

SceneVisibilityComponentStore& SceneComponentStorage::Visibility() noexcept {
    return visibility_;
}

const SceneBehaviourComponentStore& SceneComponentStorage::Behaviours() const noexcept {
    return behaviours_;
}

SceneBehaviourComponentStore& SceneComponentStorage::Behaviours() noexcept {
    return behaviours_;
}

const SceneCameraComponentStore& SceneComponentStorage::Cameras() const noexcept {
    return cameras_;
}

SceneCameraComponentStore& SceneComponentStorage::Cameras() noexcept {
    return cameras_;
}

const SceneMeshRendererComponentStore& SceneComponentStorage::MeshRenderers() const noexcept {
    return meshRenderers_;
}

SceneMeshRendererComponentStore& SceneComponentStorage::MeshRenderers() noexcept {
    return meshRenderers_;
}

const SceneLightComponentStore& SceneComponentStorage::Lights() const noexcept {
    return lights_;
}

SceneLightComponentStore& SceneComponentStorage::Lights() noexcept {
    return lights_;
}

const SceneInputComponentStore& SceneComponentStorage::Inputs() const noexcept {
    return inputs_;
}

SceneInputComponentStore& SceneComponentStorage::Inputs() noexcept {
    return inputs_;
}

const SceneRigidbodyComponentStore& SceneComponentStorage::Rigidbodies() const noexcept {
    return rigidbodies_;
}

SceneRigidbodyComponentStore& SceneComponentStorage::Rigidbodies() noexcept {
    return rigidbodies_;
}

const SceneColliderComponentStore& SceneComponentStorage::Colliders() const noexcept {
    return colliders_;
}

SceneColliderComponentStore& SceneComponentStorage::Colliders() noexcept {
    return colliders_;
}

const SceneCharacterControllerComponentStore& SceneComponentStorage::CharacterControllers() const noexcept {
    return characterControllers_;
}

SceneCharacterControllerComponentStore& SceneComponentStorage::CharacterControllers() noexcept {
    return characterControllers_;
}

const SceneJointComponentStore& SceneComponentStorage::Joints() const noexcept {
    return joints_;
}

SceneJointComponentStore& SceneComponentStorage::Joints() noexcept {
    return joints_;
}

const SceneTagsComponentStore& SceneComponentStorage::Tags() const noexcept {
    return tags_;
}

SceneTagsComponentStore& SceneComponentStorage::Tags() noexcept {
    return tags_;
}

const SceneRegionShapeComponentStore& SceneComponentStorage::RegionShapes() const noexcept {
    return regionShapes_;
}

SceneRegionShapeComponentStore& SceneComponentStorage::RegionShapes() noexcept {
    return regionShapes_;
}
const SceneGuideCurveComponentStore& SceneComponentStorage::GuideCurves() const noexcept { return guideCurves_; }
SceneGuideCurveComponentStore& SceneComponentStorage::GuideCurves() noexcept { return guideCurves_; }
const SceneContentInstanceComponentStore& SceneComponentStorage::ContentInstances() const noexcept { return contentInstances_; }
SceneContentInstanceComponentStore& SceneComponentStorage::ContentInstances() noexcept { return contentInstances_; }
const SceneStreamFocusComponentStore& SceneComponentStorage::StreamFocuses() const noexcept { return streamFocuses_; }
SceneStreamFocusComponentStore& SceneComponentStorage::StreamFocuses() noexcept { return streamFocuses_; }
const SceneWorldBackdropComponentStore& SceneComponentStorage::WorldBackdrops() const noexcept { return worldBackdrops_; }
SceneWorldBackdropComponentStore& SceneComponentStorage::WorldBackdrops() noexcept { return worldBackdrops_; }

const SceneAudioSourceComponentStore& SceneComponentStorage::AudioSources() const noexcept {
    return audioSources_;
}

SceneAudioSourceComponentStore& SceneComponentStorage::AudioSources() noexcept {
    return audioSources_;
}

const SceneAudioListenerComponentStore& SceneComponentStorage::AudioListeners() const noexcept {
    return audioListeners_;
}

SceneAudioListenerComponentStore& SceneComponentStorage::AudioListeners() noexcept {
    return audioListeners_;
}

const SceneAnimatorComponentStore& SceneComponentStorage::Animators() const noexcept { return animators_; }
SceneAnimatorComponentStore& SceneComponentStorage::Animators() noexcept { return animators_; }
const SceneUIDocumentComponentStore& SceneComponentStorage::UIDocuments() const noexcept { return uiDocuments_; }
SceneUIDocumentComponentStore& SceneComponentStorage::UIDocuments() noexcept { return uiDocuments_; }
const SceneNavigationComponentStore& SceneComponentStorage::Navigation() const noexcept { return navigation_; }
SceneNavigationComponentStore& SceneComponentStorage::Navigation() noexcept { return navigation_; }

} // namespace kb::scene
