#include "scene/prefab/ScenePrefabNodeStateWriter.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAudioListenerComponents.hpp"
#include "engine/scene/SceneAudioSourceComponents.hpp"
#include "engine/scene/SceneCameraComponents.hpp"
#include "engine/scene/SceneColliderComponents.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneInputComponents.hpp"
#include "engine/scene/SceneLightComponents.hpp"
#include "engine/scene/SceneMeshRendererComponents.hpp"
#include "engine/scene/SceneRigidbodyComponents.hpp"
#include "engine/scene/SceneTagsComponents.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/hierarchy/SceneHierarchyCache.hpp"
#include "scene/prefab/ScenePrefabOptionalComponentMask.hpp"

#include <optional>
#include <string_view>

namespace kb::scene {
namespace {

[[nodiscard]] bool Equals(const Vec3& lhs, const Vec3& rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

[[nodiscard]] bool Equals(const Quat& lhs, const Quat& rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
}

[[nodiscard]] bool HasSameLocalTransform(const TransformComponent& lhs, const TransformComponent& rhs) noexcept {
    return Equals(lhs.localPosition, rhs.localPosition) && Equals(lhs.localRotation, rhs.localRotation) && Equals(lhs.localScale, rhs.localScale);
}

[[nodiscard]] bool Equals(const VisibilityComponent& lhs, const VisibilityComponent& rhs) noexcept {
    return lhs.visible == rhs.visible;
}

[[nodiscard]] bool Equals(const CameraComponent& lhs, const CameraComponent& rhs) noexcept {
    return lhs.projection == rhs.projection && lhs.verticalFovDegrees == rhs.verticalFovDegrees && lhs.orthographicHeight == rhs.orthographicHeight
        && lhs.nearClip == rhs.nearClip && lhs.farClip == rhs.farClip && lhs.primary == rhs.primary;
}

[[nodiscard]] bool Equals(const MeshRendererComponent& lhs, const MeshRendererComponent& rhs) noexcept {
    return lhs.meshAssetId == rhs.meshAssetId && lhs.materialAssetId == rhs.materialAssetId && lhs.materialSlotAssetIds == rhs.materialSlotAssetIds
        && lhs.materialSlotOverrideCount == rhs.materialSlotOverrideCount && lhs.castsShadow == rhs.castsShadow && lhs.receivesShadow == rhs.receivesShadow;
}

[[nodiscard]] bool Equals(const LightComponent& lhs, const LightComponent& rhs) noexcept {
    return lhs.kind == rhs.kind && Equals(lhs.color, rhs.color) && lhs.intensity == rhs.intensity && lhs.range == rhs.range
        && lhs.innerConeDegrees == rhs.innerConeDegrees && lhs.outerConeDegrees == rhs.outerConeDegrees && lhs.areaWidth == rhs.areaWidth
        && lhs.areaHeight == rhs.areaHeight && lhs.contactShadowLength == rhs.contactShadowLength && lhs.volumetricScattering == rhs.volumetricScattering
        && lhs.castsShadow == rhs.castsShadow;
}

[[nodiscard]] bool Equals(const InputComponent& lhs, const InputComponent& rhs) noexcept {
    return lhs.mappingContextAssetId == rhs.mappingContextAssetId && lhs.priority == rhs.priority && lhs.enabled == rhs.enabled;
}

[[nodiscard]] bool Equals(const RigidbodyComponent& lhs, const RigidbodyComponent& rhs) noexcept {
    return lhs.bodyType == rhs.bodyType && lhs.mass == rhs.mass && Equals(lhs.linearVelocity, rhs.linearVelocity)
        && Equals(lhs.angularVelocity, rhs.angularVelocity) && lhs.gravityScale == rhs.gravityScale && lhs.useGravity == rhs.useGravity
        && lhs.lockRotation == rhs.lockRotation;
}

[[nodiscard]] bool Equals(const ColliderComponent& lhs, const ColliderComponent& rhs) noexcept {
    return lhs.shape == rhs.shape && Equals(lhs.center, rhs.center) && Equals(lhs.boxSize, rhs.boxSize) && lhs.radius == rhs.radius
        && lhs.height == rhs.height && lhs.trigger == rhs.trigger;
}

[[nodiscard]] bool Equals(const TagsComponent& lhs, const TagsComponent& rhs) noexcept {
    return TagsText(lhs) == TagsText(rhs);
}

[[nodiscard]] bool Equals(const BehaviourComponent& lhs, const BehaviourComponent& rhs) noexcept {
    return lhs.behaviourAssetId == rhs.behaviourAssetId && lhs.backend == rhs.backend && lhs.enabled == rhs.enabled && lhs.tickGroup == rhs.tickGroup
        && lhs.executionOrder == rhs.executionOrder;
}

[[nodiscard]] bool Equals(const AudioSourceComponent& lhs, const AudioSourceComponent& rhs) noexcept {
    return lhs.clipAssetId == rhs.clipAssetId && lhs.volume == rhs.volume && lhs.pitch == rhs.pitch && lhs.loop == rhs.loop && lhs.spatial == rhs.spatial
        && lhs.autoplay == rhs.autoplay && lhs.enabled == rhs.enabled && lhs.mute == rhs.mute && lhs.pan == rhs.pan && lhs.spatialBlend == rhs.spatialBlend
        && lhs.attenuationModel == rhs.attenuationModel && lhs.minDistance == rhs.minDistance && lhs.maxDistance == rhs.maxDistance && lhs.rolloff == rhs.rolloff
        && lhs.dopplerFactor == rhs.dopplerFactor;
}

[[nodiscard]] bool Equals(const AudioListenerComponent& lhs, const AudioListenerComponent& rhs) noexcept {
    return lhs.primary == rhs.primary && lhs.enabled == rhs.enabled;
}

template <typename T, typename Components>
void WriteOptionalComponent(Components components, SceneEntity entity, const std::optional<T>& component) {
    if (component.has_value()) {
        const T* current = components.TryGet(entity);
        if (current == nullptr || !Equals(*current, *component)) {
            components.Set(entity, *component);
        }
    } else if (components.TryGet(entity) != nullptr) {
        components.Remove(entity);
    }
}

[[nodiscard]] std::optional<std::string_view> CachedNameView(const SceneState& state, SceneEntity entity) noexcept {
    const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(entity);
    if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex && denseIndex < state.denseEntityNames.size()) {
        const std::string& name = state.denseEntityNames[denseIndex];
        if (!name.empty()) {
            return std::string_view{ name };
        }
    }

    const auto name = state.entityNames.find(entity.Id());
    if (name != state.entityNames.end()) {
        return std::string_view{ name->second };
    }
    return std::nullopt;
}

[[nodiscard]] bool CachedNameMatches(const SceneState& state, SceneEntity entity, std::string_view expectedName) noexcept {
    const std::optional<std::string_view> cachedName = CachedNameView(state, entity);
    return cachedName.has_value() && *cachedName == expectedName;
}

} // namespace

void ScenePrefabNodeStateWriter::Write(Scene& scene, SceneObject object, SceneObject parent, const ScenePrefabNodeDesc& node) {
    SceneState& state = SceneAccess::State(scene);
    ScenePrefabNodeStateWriterContext context{ scene, state };
    Write(context, object, parent, node);
}

ScenePrefabNodeStateWriterContext::ScenePrefabNodeStateWriterContext(Scene& scene, SceneState& sceneState) noexcept
    : state(sceneState)
    , previousSuppressPrefabDirtyTracking(sceneState.suppressPrefabDirtyTracking)
    , entities(scene.Entities())
    , hierarchy(scene.Hierarchy())
    , transforms(scene.Transforms())
    , visibility(scene.Components().Visibility())
    , cameras(scene.Components().Cameras())
    , meshRenderers(scene.Components().MeshRenderers())
    , lights(scene.Components().Lights())
    , inputs(scene.Components().Inputs())
    , rigidbodies(scene.Components().Rigidbodies())
    , colliders(scene.Components().Colliders())
    , tags(scene.Components().Tags())
    , behaviours(scene.Components().Behaviours())
    , audioSources(scene.Components().AudioSources())
    , audioListeners(scene.Components().AudioListeners()) {
    state.suppressPrefabDirtyTracking = true;
}

ScenePrefabNodeStateWriterContext::~ScenePrefabNodeStateWriterContext() noexcept {
    state.suppressPrefabDirtyTracking = previousSuppressPrefabDirtyTracking;
}

void ScenePrefabNodeStateWriter::Write(ScenePrefabNodeStateWriterContext& context, SceneObject object, SceneObject parent, const ScenePrefabNodeDesc& node) {
    if (!object.IsValid() || !context.entities.IsAlive(object)) {
        return;
    }

    const SceneEntity entity = object.Entity();
    if (!CachedNameMatches(context.state, entity, node.name)) {
        context.entities.SetName(entity, node.name);
    }

    if (SceneHierarchyCache::Parent(context.state, entity) != parent.Entity()) {
        static_cast<void>(context.hierarchy.SetParent(entity, parent.Entity()));
    }

    if (const TransformComponent* currentTransform = context.transforms.TryGet(entity); currentTransform == nullptr || !HasSameLocalTransform(*currentTransform, node.transform)) {
        context.transforms.Set(entity, node.transform);
    }

    if (const VisibilityComponent* currentVisibility = context.visibility.TryGet(entity); currentVisibility == nullptr || !Equals(*currentVisibility, node.visibility)) {
        context.visibility.Set(entity, node.visibility);
    }

    const ScenePrefabOptionalComponentMaskMatch componentMask = ScenePrefabOptionalComponentMaskMatches(context.state, entity, node.components);
    if (!componentMask.available || !componentMask.matches || node.components.camera.has_value()) {
        WriteOptionalComponent(context.cameras, entity, node.components.camera);
    }
    if (!componentMask.available || !componentMask.matches || node.components.meshRenderer.has_value()) {
        WriteOptionalComponent(context.meshRenderers, entity, node.components.meshRenderer);
    }
    if (!componentMask.available || !componentMask.matches || node.components.light.has_value()) {
        WriteOptionalComponent(context.lights, entity, node.components.light);
    }
    if (!componentMask.available || !componentMask.matches || node.components.input.has_value()) {
        WriteOptionalComponent(context.inputs, entity, node.components.input);
    }
    if (!componentMask.available || !componentMask.matches || node.components.rigidbody.has_value()) {
        WriteOptionalComponent(context.rigidbodies, entity, node.components.rigidbody);
    }
    if (!componentMask.available || !componentMask.matches || node.components.collider.has_value()) {
        WriteOptionalComponent(context.colliders, entity, node.components.collider);
    }
    if (!componentMask.available || !componentMask.matches || node.components.tags.has_value()) {
        WriteOptionalComponent(context.tags, entity, node.components.tags);
    }
    if (!componentMask.available || !componentMask.matches || node.components.behaviour.has_value()) {
        WriteOptionalComponent(context.behaviours, entity, node.components.behaviour);
    }
    if (!componentMask.available || !componentMask.matches || node.components.audioSource.has_value()) {
        WriteOptionalComponent(context.audioSources, entity, node.components.audioSource);
    }
    if (!componentMask.available || !componentMask.matches || node.components.audioListener.has_value()) {
        WriteOptionalComponent(context.audioListeners, entity, node.components.audioListener);
    }
}

} // namespace kb::scene
