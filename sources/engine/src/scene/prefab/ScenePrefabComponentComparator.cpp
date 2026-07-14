#include "scene/prefab/ScenePrefabComponentComparator.hpp"

#include "engine/scene/SceneComponents.hpp"

namespace kb::scene {
namespace {

[[nodiscard]] bool Equal(Vec3 lhs, Vec3 rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

[[nodiscard]] bool Equal(const CameraComponent& lhs, const CameraComponent& rhs) noexcept {
    return lhs.projection == rhs.projection
        && lhs.verticalFovDegrees == rhs.verticalFovDegrees
        && lhs.orthographicHeight == rhs.orthographicHeight
        && lhs.nearClip == rhs.nearClip
        && lhs.farClip == rhs.farClip
        && lhs.primary == rhs.primary;
}

[[nodiscard]] bool Equal(const MeshRendererComponent& lhs, const MeshRendererComponent& rhs) noexcept {
    return lhs.meshAssetId == rhs.meshAssetId
        && lhs.materialAssetId == rhs.materialAssetId
        && lhs.materialSlotAssetIds == rhs.materialSlotAssetIds
        && lhs.materialSlotOverrideCount == rhs.materialSlotOverrideCount
        && lhs.castsShadow == rhs.castsShadow
        && lhs.receivesShadow == rhs.receivesShadow;
}

[[nodiscard]] bool Equal(const LightComponent& lhs, const LightComponent& rhs) noexcept {
    return lhs.kind == rhs.kind
        && Equal(lhs.color, rhs.color)
        && lhs.intensity == rhs.intensity
        && lhs.range == rhs.range
        && lhs.innerConeDegrees == rhs.innerConeDegrees
        && lhs.outerConeDegrees == rhs.outerConeDegrees
        && lhs.areaWidth == rhs.areaWidth
        && lhs.areaHeight == rhs.areaHeight
        && lhs.contactShadowLength == rhs.contactShadowLength
        && lhs.volumetricScattering == rhs.volumetricScattering
        && lhs.castsShadow == rhs.castsShadow;
}

[[nodiscard]] bool Equal(const InputComponent& lhs, const InputComponent& rhs) noexcept {
    return lhs.mappingContextAssetId == rhs.mappingContextAssetId
        && lhs.priority == rhs.priority
        && lhs.enabled == rhs.enabled
        && lhs.localUser == rhs.localUser;
}

[[nodiscard]] bool Equal(const RigidbodyComponent& lhs, const RigidbodyComponent& rhs) noexcept {
    return lhs.bodyType == rhs.bodyType
        && lhs.mass == rhs.mass
        && Equal(lhs.linearVelocity, rhs.linearVelocity)
        && Equal(lhs.angularVelocity, rhs.angularVelocity)
        && lhs.gravityScale == rhs.gravityScale
        && lhs.useGravity == rhs.useGravity
        && lhs.lockRotation == rhs.lockRotation;
}

[[nodiscard]] bool Equal(const ColliderComponent& lhs, const ColliderComponent& rhs) noexcept {
    return lhs.shape == rhs.shape
        && Equal(lhs.center, rhs.center)
        && Equal(lhs.boxSize, rhs.boxSize)
        && lhs.radius == rhs.radius
        && lhs.height == rhs.height
        && lhs.trigger == rhs.trigger;
}

[[nodiscard]] bool Equal(const TagsComponent& lhs, const TagsComponent& rhs) noexcept {
    return TagsText(lhs) == TagsText(rhs);
}

[[nodiscard]] bool Equal(const BehaviourComponent& lhs, const BehaviourComponent& rhs) noexcept {
    return lhs.behaviourAssetId == rhs.behaviourAssetId
        && lhs.backend == rhs.backend
        && lhs.enabled == rhs.enabled
        && lhs.tickGroup == rhs.tickGroup
        && lhs.executionOrder == rhs.executionOrder;
}

[[nodiscard]] bool Equal(const AudioSourceComponent& lhs, const AudioSourceComponent& rhs) noexcept {
    return lhs.clipAssetId == rhs.clipAssetId
        && lhs.volume == rhs.volume
        && lhs.pitch == rhs.pitch
        && lhs.loop == rhs.loop
        && lhs.spatial == rhs.spatial
        && lhs.autoplay == rhs.autoplay
        && lhs.enabled == rhs.enabled
        && lhs.mute == rhs.mute
        && lhs.pan == rhs.pan
        && lhs.spatialBlend == rhs.spatialBlend
        && lhs.attenuationModel == rhs.attenuationModel
        && lhs.minDistance == rhs.minDistance
        && lhs.maxDistance == rhs.maxDistance
        && lhs.rolloff == rhs.rolloff
        && lhs.dopplerFactor == rhs.dopplerFactor;
}

[[nodiscard]] bool Equal(const AudioListenerComponent& lhs, const AudioListenerComponent& rhs) noexcept {
    return lhs.primary == rhs.primary
        && lhs.enabled == rhs.enabled;
}

template <typename T>
[[nodiscard]] bool EqualOptionalComponent(const T* actual, const std::optional<T>& expected) noexcept {
    if (actual == nullptr) {
        return !expected.has_value();
    }
    return expected.has_value() && Equal(*actual, *expected);
}

} // namespace

ScenePrefabOverrideFlag ScenePrefabComponentComparator::Compare(SceneComponents components, SceneEntity entity, const ScenePrefabNodeComponents& expected) noexcept {
    ScenePrefabOverrideFlag flags = ScenePrefabOverrideFlag::None;
    if (!EqualOptionalComponent(components.Cameras().TryGet(entity), expected.camera)) {
        flags |= ScenePrefabOverrideFlag::Camera;
    }
    if (!EqualOptionalComponent(components.MeshRenderers().TryGet(entity), expected.meshRenderer)) {
        flags |= ScenePrefabOverrideFlag::MeshRenderer;
    }
    if (!EqualOptionalComponent(components.Lights().TryGet(entity), expected.light)) {
        flags |= ScenePrefabOverrideFlag::Light;
    }
    if (!EqualOptionalComponent(components.Inputs().TryGet(entity), expected.input)) {
        flags |= ScenePrefabOverrideFlag::Input;
    }
    if (!EqualOptionalComponent(components.Rigidbodies().TryGet(entity), expected.rigidbody)) {
        flags |= ScenePrefabOverrideFlag::Rigidbody;
    }
    if (!EqualOptionalComponent(components.Colliders().TryGet(entity), expected.collider)) {
        flags |= ScenePrefabOverrideFlag::Collider;
    }
    if (!EqualOptionalComponent(components.Tags().TryGet(entity), expected.tags)) {
        flags |= ScenePrefabOverrideFlag::Tags;
    }
    if (!EqualOptionalComponent(components.Behaviours().TryGet(entity), expected.behaviour)) {
        flags |= ScenePrefabOverrideFlag::Behaviour;
    }
    if (!EqualOptionalComponent(components.AudioSources().TryGet(entity), expected.audioSource)) {
        flags |= ScenePrefabOverrideFlag::AudioSource;
    }
    if (!EqualOptionalComponent(components.AudioListeners().TryGet(entity), expected.audioListener)) {
        flags |= ScenePrefabOverrideFlag::AudioListener;
    }
    return flags;
}

} // namespace kb::scene
