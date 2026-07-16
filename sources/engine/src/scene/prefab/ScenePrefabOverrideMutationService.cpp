#include "scene/prefab/ScenePrefabOverrideMutationService.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabHasher.hpp"
#include "scene/prefab/ScenePrefabHashBuilder.hpp"
#include "scene/prefab/ScenePrefabInstanceTopology.hpp"
#include "scene/prefab/ScenePrefabInstanceSynchronizer.hpp"
#include "scene/prefab/ScenePrefabNestedResolver.hpp"
#include "scene/prefab/ScenePrefabNodeStateWriter.hpp"
#include "scene/prefab/ScenePrefabOverrideApplier.hpp"
#include "scene/prefab/ScenePrefabOptionalComponentMask.hpp"
#include "scene/prefab/ScenePrefabOverridePropertyMutationService.hpp"
#include "scene/prefab/ScenePrefabOverrideReverter.hpp"
#include "scene/prefab/ScenePrefabOverrideTargetResolver.hpp"
#include "scene/prefab/ScenePrefabTemplateOverrideService.hpp"
#include "scene/prefab/ScenePrefabVariantOverrideService.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace kb::scene {
namespace {

enum class BatchTemplateApplyResult {
    Applied,
    NotEligible,
};

enum class BatchTemplateRevertResult {
    Reverted,
    NotEligible,
};

struct LivePrefabComponentReaders {
    SceneVisibilityComponents visibility;
    SceneCameraComponents cameras;
    SceneMeshRendererComponents meshRenderers;
    SceneLightComponents lights;
    SceneInputComponents inputs;
    SceneRigidbodyComponents rigidbodies;
    SceneColliderComponents colliders;
    SceneTagsComponents tags;
    SceneBehaviourComponents behaviours;
    SceneAudioSourceComponents audioSources;
    SceneAudioListenerComponents audioListeners;
};

[[nodiscard]] bool SameObjects(std::span<const SceneObject> lhs, std::span<const SceneObject> rhs) noexcept {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (lhs[index].Entity() != rhs[index].Entity()) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::vector<std::uint32_t> BuildExpectedChildCounts(std::span<const ScenePrefabNodeDesc> nodes) {
    std::vector<std::uint32_t> childCounts(nodes.size(), 0U);
    for (const ScenePrefabNodeDesc& node : nodes) {
        if (node.parentNode != ScenePrefabNodeDesc::NoParent && node.parentNode < childCounts.size()) {
            ++childCounts[node.parentNode];
        }
    }
    return childCounts;
}

[[nodiscard]] std::shared_ptr<const std::vector<std::uint64_t>> BuildSharedNodeIds(const ScenePrefab& prefab) {
    auto nodeIds = std::make_shared<std::vector<std::uint64_t>>();
    nodeIds->reserve(prefab.NodeCount());
    for (const ScenePrefabNodeDesc& node : prefab.Nodes()) {
        nodeIds->push_back(node.stableId);
    }
    return nodeIds;
}

[[nodiscard]] LivePrefabComponentReaders BuildLivePrefabComponentReaders(SceneComponents components) noexcept {
    return LivePrefabComponentReaders{
        .visibility = components.Visibility(),
        .cameras = components.Cameras(),
        .meshRenderers = components.MeshRenderers(),
        .lights = components.Lights(),
        .inputs = components.Inputs(),
        .rigidbodies = components.Rigidbodies(),
        .colliders = components.Colliders(),
        .tags = components.Tags(),
        .behaviours = components.Behaviours(),
        .audioSources = components.AudioSources(),
        .audioListeners = components.AudioListeners(),
    };
}

[[nodiscard]] std::size_t CachedChildCount(const SceneState& state, SceneEntity entity) noexcept {
    const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(entity);
    if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex && denseIndex < state.denseHierarchyChildren.size()) {
        const std::vector<SceneEntity>& denseChildren = state.denseHierarchyChildren[denseIndex];
        if (!denseChildren.empty()) {
            return denseChildren.size();
        }
    }

    const auto children = state.hierarchyChildren.find(entity.Id());
    return children == state.hierarchyChildren.end() ? 0U : children->second.size();
}

[[nodiscard]] SceneEntity CachedParent(const SceneState& state, SceneEntity entity) noexcept {
    const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(entity);
    if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex && denseIndex < state.denseHierarchyParents.size()) {
        return state.denseHierarchyParents[denseIndex];
    }

    const auto parent = state.hierarchyParents.find(entity.Id());
    return parent == state.hierarchyParents.end() ? SceneEntity{} : parent->second;
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

[[nodiscard]] bool Equals(const Vec3& lhs, const Vec3& rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

[[nodiscard]] bool Equals(const Quat& lhs, const Quat& rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
}

[[nodiscard]] bool Equals(const TransformComponent& lhs, const TransformComponent& rhs) noexcept {
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
        && lhs.dopplerFactor == rhs.dopplerFactor
        && AudioSourceOutputBus(lhs) == AudioSourceOutputBus(rhs);
}

[[nodiscard]] bool Equals(const AudioListenerComponent& lhs, const AudioListenerComponent& rhs) noexcept {
    return lhs.primary == rhs.primary && lhs.enabled == rhs.enabled;
}

template <typename T, typename Components>
[[nodiscard]] bool OptionalComponentMatches(Components components, SceneEntity entity, const std::optional<T>& expected) {
    const T* current = components.TryGet(entity);
    if (expected.has_value()) {
        return current != nullptr && Equals(*current, *expected);
    }
    return current == nullptr;
}

[[nodiscard]] bool LiveComponentsMatchTemplate(
    const LivePrefabComponentReaders& readers,
    SceneEntity entity,
    const ScenePrefabNodeComponents& expected,
    bool exactMaskValidated) {
    if (exactMaskValidated) {
        return (!expected.camera.has_value() || OptionalComponentMatches(readers.cameras, entity, expected.camera)) &&
            (!expected.meshRenderer.has_value() || OptionalComponentMatches(readers.meshRenderers, entity, expected.meshRenderer)) &&
            (!expected.light.has_value() || OptionalComponentMatches(readers.lights, entity, expected.light)) &&
            (!expected.input.has_value() || OptionalComponentMatches(readers.inputs, entity, expected.input)) &&
            (!expected.rigidbody.has_value() || OptionalComponentMatches(readers.rigidbodies, entity, expected.rigidbody)) &&
            (!expected.collider.has_value() || OptionalComponentMatches(readers.colliders, entity, expected.collider)) &&
            (!expected.tags.has_value() || OptionalComponentMatches(readers.tags, entity, expected.tags)) &&
            (!expected.behaviour.has_value() || OptionalComponentMatches(readers.behaviours, entity, expected.behaviour)) &&
            (!expected.audioSource.has_value() || OptionalComponentMatches(readers.audioSources, entity, expected.audioSource)) &&
            (!expected.audioListener.has_value() || OptionalComponentMatches(readers.audioListeners, entity, expected.audioListener));
    }

    return OptionalComponentMatches(readers.cameras, entity, expected.camera) &&
        OptionalComponentMatches(readers.meshRenderers, entity, expected.meshRenderer) &&
        OptionalComponentMatches(readers.lights, entity, expected.light) &&
        OptionalComponentMatches(readers.inputs, entity, expected.input) &&
        OptionalComponentMatches(readers.rigidbodies, entity, expected.rigidbody) &&
        OptionalComponentMatches(readers.colliders, entity, expected.collider) &&
        OptionalComponentMatches(readers.tags, entity, expected.tags) &&
        OptionalComponentMatches(readers.behaviours, entity, expected.behaviour) &&
        OptionalComponentMatches(readers.audioSources, entity, expected.audioSource) &&
        OptionalComponentMatches(readers.audioListeners, entity, expected.audioListener);
}

[[nodiscard]] bool LiveNodeMatchesTemplate(
    const SceneState& state,
    const LivePrefabComponentReaders& componentReaders,
    SceneTransforms transforms,
    SceneEntities entities,
    const ScenePrefabNodeDesc& node,
    const ScenePrefabInstanceRecord& instance,
    std::uint32_t nodeIndex,
    std::span<const std::uint32_t> expectedChildCounts,
    bool validateTopology) {
    const std::span<const SceneObject> objects = instance.Objects();
    if (nodeIndex >= objects.size() || nodeIndex >= expectedChildCounts.size()) {
        return false;
    }

    const SceneObject object = objects[nodeIndex];
    if (!object.IsValid() || !entities.IsAlive(object)) {
        return false;
    }

    const SceneEntity entity = object.Entity();
    if (validateTopology) {
        const SceneObject expectedParent = ScenePrefabInstanceTopology::ExpectedParent(node, instance);
        if (CachedParent(state, entity) != expectedParent.Entity()) {
            return false;
        }
        if (CachedChildCount(state, entity) != expectedChildCounts[nodeIndex]) {
            return false;
        }
    }

    if (const std::optional<std::string_view> cachedName = CachedNameView(state, entity); cachedName.has_value()) {
        if (*cachedName != node.name) {
            return false;
        }
    } else if (entities.Name(entity) != node.name) {
        return false;
    }

    const TransformComponent* currentTransform = transforms.TryGet(entity);
    if (currentTransform == nullptr || !Equals(*currentTransform, node.transform)) {
        return false;
    }
    if (!Equals(componentReaders.visibility.Get(entity), node.visibility)) {
        return false;
    }
    const ScenePrefabOptionalComponentMaskMatch componentMask = ScenePrefabOptionalComponentMaskMatches(state, entity, node.components);
    if (componentMask.available && !componentMask.matches) {
        return false;
    }
    return LiveComponentsMatchTemplate(componentReaders, entity, node.components, componentMask.available);
}

void MixLiveSceneComponents(std::uint64_t& hash, SceneComponents components, SceneEntity entity) noexcept {
    const CameraComponent* camera = components.Cameras().TryGet(entity);
    ScenePrefabHashBuilder::Mix(hash, camera != nullptr ? 1U : 0U);
    if (camera != nullptr) {
        ScenePrefabHashBuilder::Mix(hash, static_cast<std::uint64_t>(camera->projection));
        ScenePrefabHashBuilder::MixFloat(hash, camera->verticalFovDegrees);
        ScenePrefabHashBuilder::MixFloat(hash, camera->orthographicHeight);
        ScenePrefabHashBuilder::MixFloat(hash, camera->nearClip);
        ScenePrefabHashBuilder::MixFloat(hash, camera->farClip);
        ScenePrefabHashBuilder::Mix(hash, camera->primary ? 1U : 0U);
    }

    const MeshRendererComponent* meshRenderer = components.MeshRenderers().TryGet(entity);
    ScenePrefabHashBuilder::Mix(hash, meshRenderer != nullptr ? 1U : 0U);
    if (meshRenderer != nullptr) {
        ScenePrefabHashBuilder::Mix(hash, meshRenderer->meshAssetId);
        ScenePrefabHashBuilder::Mix(hash, meshRenderer->materialAssetId);
        ScenePrefabHashBuilder::Mix(hash, meshRenderer->materialSlotOverrideCount);
        for (std::uint32_t slotIndex = 0U; slotIndex < meshRenderer->materialSlotOverrideCount && slotIndex < kMaxMeshRendererMaterialSlotOverrides; ++slotIndex) {
            ScenePrefabHashBuilder::Mix(hash, meshRenderer->materialSlotAssetIds[slotIndex]);
        }
        ScenePrefabHashBuilder::Mix(hash, meshRenderer->castsShadow ? 1U : 0U);
        ScenePrefabHashBuilder::Mix(hash, meshRenderer->receivesShadow ? 1U : 0U);
    }

    const LightComponent* light = components.Lights().TryGet(entity);
    ScenePrefabHashBuilder::Mix(hash, light != nullptr ? 1U : 0U);
    if (light != nullptr) {
        ScenePrefabHashBuilder::Mix(hash, static_cast<std::uint64_t>(light->kind));
        ScenePrefabHashBuilder::MixVec3(hash, light->color);
        ScenePrefabHashBuilder::MixFloat(hash, light->intensity);
        ScenePrefabHashBuilder::MixFloat(hash, light->range);
        ScenePrefabHashBuilder::MixFloat(hash, light->innerConeDegrees);
        ScenePrefabHashBuilder::MixFloat(hash, light->outerConeDegrees);
        ScenePrefabHashBuilder::MixFloat(hash, light->areaWidth);
        ScenePrefabHashBuilder::MixFloat(hash, light->areaHeight);
        ScenePrefabHashBuilder::MixFloat(hash, light->contactShadowLength);
        ScenePrefabHashBuilder::MixFloat(hash, light->volumetricScattering);
        ScenePrefabHashBuilder::Mix(hash, light->castsShadow ? 1U : 0U);
    }

    const InputComponent* input = components.Inputs().TryGet(entity);
    ScenePrefabHashBuilder::Mix(hash, input != nullptr ? 1U : 0U);
    if (input != nullptr) {
        ScenePrefabHashBuilder::Mix(hash, input->mappingContextAssetId);
        ScenePrefabHashBuilder::Mix(hash, static_cast<std::uint64_t>(static_cast<std::uint32_t>(input->priority)));
        ScenePrefabHashBuilder::Mix(hash, input->enabled ? 1U : 0U);
    }

    const RigidbodyComponent* rigidbody = components.Rigidbodies().TryGet(entity);
    ScenePrefabHashBuilder::Mix(hash, rigidbody != nullptr ? 1U : 0U);
    if (rigidbody != nullptr) {
        ScenePrefabHashBuilder::Mix(hash, static_cast<std::uint64_t>(rigidbody->bodyType));
        ScenePrefabHashBuilder::MixFloat(hash, rigidbody->mass);
        ScenePrefabHashBuilder::MixVec3(hash, rigidbody->linearVelocity);
        ScenePrefabHashBuilder::MixVec3(hash, rigidbody->angularVelocity);
        ScenePrefabHashBuilder::MixFloat(hash, rigidbody->gravityScale);
        ScenePrefabHashBuilder::Mix(hash, rigidbody->useGravity ? 1U : 0U);
        ScenePrefabHashBuilder::Mix(hash, rigidbody->lockRotation ? 1U : 0U);
    }

    const ColliderComponent* collider = components.Colliders().TryGet(entity);
    ScenePrefabHashBuilder::Mix(hash, collider != nullptr ? 1U : 0U);
    if (collider != nullptr) {
        ScenePrefabHashBuilder::Mix(hash, static_cast<std::uint64_t>(collider->shape));
        ScenePrefabHashBuilder::MixVec3(hash, collider->center);
        ScenePrefabHashBuilder::MixVec3(hash, collider->boxSize);
        ScenePrefabHashBuilder::MixFloat(hash, collider->radius);
        ScenePrefabHashBuilder::MixFloat(hash, collider->height);
        ScenePrefabHashBuilder::Mix(hash, collider->trigger ? 1U : 0U);
    }

    const TagsComponent* tags = components.Tags().TryGet(entity);
    ScenePrefabHashBuilder::Mix(hash, tags != nullptr ? 1U : 0U);
    if (tags != nullptr) {
        for (char character : TagsText(*tags)) {
            ScenePrefabHashBuilder::Mix(hash, static_cast<std::uint64_t>(static_cast<unsigned char>(character)));
        }
    }

    const BehaviourComponent* behaviour = components.Behaviours().TryGet(entity);
    ScenePrefabHashBuilder::Mix(hash, behaviour != nullptr ? 1U : 0U);
    if (behaviour != nullptr) {
        ScenePrefabHashBuilder::Mix(hash, behaviour->behaviourAssetId);
        ScenePrefabHashBuilder::Mix(hash, static_cast<std::uint64_t>(behaviour->backend));
        ScenePrefabHashBuilder::Mix(hash, behaviour->enabled ? 1U : 0U);
        ScenePrefabHashBuilder::Mix(hash, static_cast<std::uint64_t>(behaviour->tickGroup));
        ScenePrefabHashBuilder::Mix(hash, static_cast<std::uint64_t>(static_cast<std::uint32_t>(behaviour->executionOrder)));
    }

    const AudioSourceComponent* audioSource = components.AudioSources().TryGet(entity);
    ScenePrefabHashBuilder::Mix(hash, audioSource != nullptr ? 1U : 0U);
    if (audioSource != nullptr) {
        ScenePrefabHashBuilder::Mix(hash, audioSource->clipAssetId);
        ScenePrefabHashBuilder::MixFloat(hash, audioSource->volume);
        ScenePrefabHashBuilder::MixFloat(hash, audioSource->pitch);
        ScenePrefabHashBuilder::Mix(hash, audioSource->loop ? 1U : 0U);
        ScenePrefabHashBuilder::Mix(hash, audioSource->spatial ? 1U : 0U);
        ScenePrefabHashBuilder::Mix(hash, audioSource->autoplay ? 1U : 0U);
        ScenePrefabHashBuilder::Mix(hash, audioSource->enabled ? 1U : 0U);
        ScenePrefabHashBuilder::Mix(hash, audioSource->mute ? 1U : 0U);
        ScenePrefabHashBuilder::MixFloat(hash, audioSource->pan);
        ScenePrefabHashBuilder::MixFloat(hash, audioSource->spatialBlend);
        ScenePrefabHashBuilder::Mix(hash, static_cast<std::uint64_t>(audioSource->attenuationModel));
        ScenePrefabHashBuilder::MixFloat(hash, audioSource->minDistance);
        ScenePrefabHashBuilder::MixFloat(hash, audioSource->maxDistance);
        ScenePrefabHashBuilder::MixFloat(hash, audioSource->rolloff);
        ScenePrefabHashBuilder::MixFloat(hash, audioSource->dopplerFactor);
        ScenePrefabHashBuilder::MixString(hash, AudioSourceOutputBus(*audioSource));
    }

    const AudioListenerComponent* audioListener = components.AudioListeners().TryGet(entity);
    ScenePrefabHashBuilder::Mix(hash, audioListener != nullptr ? 1U : 0U);
    if (audioListener != nullptr) {
        ScenePrefabHashBuilder::Mix(hash, audioListener->primary ? 1U : 0U);
        ScenePrefabHashBuilder::Mix(hash, audioListener->enabled ? 1U : 0U);
    }
}

[[nodiscard]] std::optional<std::uint64_t> FingerprintInstanceAsTemplate(
    Scene& scene,
    const SceneState& state,
    const ScenePrefab& sourcePrefab,
    const ScenePrefabInstanceRecord& instance,
    std::span<const std::uint32_t> expectedChildCounts) {
    constexpr std::uint64_t fnvOffset = 14695981039346656037ULL;

    const std::span<const ScenePrefabNodeDesc> nodes = sourcePrefab.Nodes();
    const std::span<const SceneObject> objects = instance.Objects();
    if (objects.size() != nodes.size() || expectedChildCounts.size() != nodes.size()) {
        return std::nullopt;
    }

    std::uint64_t hash = fnvOffset;
    SceneComponents components = scene.Components();
    ScenePrefabHashBuilder::Mix(hash, nodes.size());
    for (std::uint32_t nodeIndex = 0; nodeIndex < static_cast<std::uint32_t>(nodes.size()); ++nodeIndex) {
        const ScenePrefabNodeDesc& sourceNode = nodes[nodeIndex];
        const SceneObject object = objects[nodeIndex];
        if (!object.IsValid() || !scene.Entities().IsAlive(object)) {
            return std::nullopt;
        }

        const SceneObject expectedParent = ScenePrefabInstanceTopology::ExpectedParent(sourceNode, instance);
        const SceneEntity entity = object.Entity();
        if (CachedParent(state, entity) != expectedParent.Entity()) {
            return std::nullopt;
        }
        if (CachedChildCount(state, entity) != expectedChildCounts[nodeIndex]) {
            return std::nullopt;
        }

        ScenePrefabHashBuilder::Mix(hash, sourceNode.stableId);
        if (const std::optional<std::string_view> cachedName = CachedNameView(state, entity); cachedName.has_value()) {
            ScenePrefabHashBuilder::MixString(hash, *cachedName);
        } else {
            ScenePrefabHashBuilder::MixString(hash, scene.Entities().Name(entity));
        }
        ScenePrefabHashBuilder::MixString(hash, sourceNode.nestedPrefabGuid);
        ScenePrefabHashBuilder::Mix(hash, sourceNode.nestedPrefabOverrides.size());
        for (const ScenePrefabPropertyOverride& property : sourceNode.nestedPrefabOverrides) {
            ScenePrefabHashBuilder::Mix(hash, property.nodeIndex);
            ScenePrefabHashBuilder::Mix(hash, property.nodeId);
            ScenePrefabHashBuilder::MixString(hash, property.propertyPath);
            ScenePrefabHashBuilder::MixString(hash, property.value);
            ScenePrefabHashBuilder::Mix(hash, static_cast<std::uint32_t>(property.flag));
        }
        ScenePrefabHashBuilder::Mix(hash, sourceNode.parentNode);
        ScenePrefabHashBuilder::MixTransform(hash, scene.Transforms().Get(entity));
        ScenePrefabHashBuilder::Mix(hash, components.Visibility().Get(entity).visible ? 1U : 0U);
        MixLiveSceneComponents(hash, components, entity);
    }
    return hash;
}

[[nodiscard]] bool InstanceMatchesCandidateTemplate(
    const SceneState& state,
    const LivePrefabComponentReaders& componentReaders,
    SceneTransforms transforms,
    SceneEntities entities,
    const ScenePrefab& candidate,
    const ScenePrefabInstanceRecord& instance,
    std::span<const std::uint32_t> expectedChildCounts) {
    const std::span<const ScenePrefabNodeDesc> nodes = candidate.Nodes();
    const std::span<const SceneObject> objects = instance.Objects();
    if (objects.size() != nodes.size() || expectedChildCounts.size() != nodes.size()) {
        return false;
    }

    for (std::uint32_t nodeIndex = 0; nodeIndex < static_cast<std::uint32_t>(nodes.size()); ++nodeIndex) {
        if (!LiveNodeMatchesTemplate(state, componentReaders, transforms, entities, nodes[nodeIndex], instance, nodeIndex, expectedChildCounts, true)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool InstanceDirtyNodesMatchCandidateTemplate(
    const SceneState& state,
    const LivePrefabComponentReaders& componentReaders,
    SceneTransforms transforms,
    SceneEntities entities,
    const ScenePrefab& candidate,
    const ScenePrefabInstanceRecord& instance,
    std::span<const std::uint32_t> expectedChildCounts,
    std::span<const std::uint32_t> dirtyNodes) {
    const std::span<const ScenePrefabNodeDesc> nodes = candidate.Nodes();
    const std::span<const SceneObject> objects = instance.Objects();
    if (objects.size() != nodes.size() || expectedChildCounts.size() != nodes.size()) {
        return false;
    }

    for (const std::uint32_t nodeIndex : dirtyNodes) {
        if (nodeIndex >= nodes.size()) {
            return false;
        }
        if (!LiveNodeMatchesTemplate(state, componentReaders, transforms, entities, nodes[nodeIndex], instance, nodeIndex, expectedChildCounts, false)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool DirtyNodesContainAll(std::span<const std::uint32_t> dirtyNodes, std::span<const std::uint32_t> requiredNodes) noexcept {
    for (const std::uint32_t requiredNode : requiredNodes) {
        if (std::ranges::find(dirtyNodes, requiredNode) == dirtyNodes.end()) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool InstanceHasExpectedTopology(
    SceneEntities entities,
    const SceneState& state,
    const ScenePrefab& prefab,
    const ScenePrefabInstanceRecord& instance,
    std::span<const std::uint32_t> expectedChildCounts) {
    const std::span<const ScenePrefabNodeDesc> nodes = prefab.Nodes();
    const std::span<const SceneObject> objects = instance.Objects();
    if (objects.size() != nodes.size() || expectedChildCounts.size() != nodes.size()) {
        return false;
    }

    for (std::uint32_t nodeIndex = 0U; nodeIndex < static_cast<std::uint32_t>(nodes.size()); ++nodeIndex) {
        const SceneObject object = objects[nodeIndex];
        if (!object.IsValid() || !entities.IsAlive(object)) {
            return false;
        }

        const SceneEntity entity = object.Entity();
        const SceneObject expectedParent = ScenePrefabInstanceTopology::ExpectedParent(nodes[nodeIndex], instance);
        if (CachedParent(state, entity) != expectedParent.Entity()) {
            return false;
        }
        if (CachedChildCount(state, entity) != expectedChildCounts[nodeIndex]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] BatchTemplateRevertResult TryRevertSamePrefabBatchFast(
    Scene& scene,
    SceneState& state,
    std::span<const ScenePrefabInstanceHandle> handles) {
    if (handles.size() < 2U) {
        return BatchTemplateRevertResult::NotEligible;
    }

    std::vector<ScenePrefabInstanceRecord*> instances;
    instances.reserve(handles.size());
    const ScenePrefab* prefab = nullptr;
    ScenePrefabHandle prefabHandle;
    for (const ScenePrefabInstanceHandle handle : handles) {
        ScenePrefabInstanceRecord* instance = state.prefabInstances.FindMutable(handle);
        if (instance == nullptr) {
            return BatchTemplateRevertResult::NotEligible;
        }
        if (!prefabHandle.IsValid()) {
            prefabHandle = instance->prefab;
        } else if (instance->prefab != prefabHandle) {
            return BatchTemplateRevertResult::NotEligible;
        }

        const ScenePrefab* resolvedPrefab = ScenePrefabOverrideTargetResolver::ResolveReadPrefab(state, *instance);
        if (resolvedPrefab == nullptr) {
            return BatchTemplateRevertResult::NotEligible;
        }
        if (prefab == nullptr) {
            prefab = resolvedPrefab;
        } else if (prefab != resolvedPrefab) {
            return BatchTemplateRevertResult::NotEligible;
        }
        instances.push_back(instance);
    }

    if (prefab == nullptr || prefab->Empty()) {
        return BatchTemplateRevertResult::NotEligible;
    }

    const std::vector<std::uint32_t> expectedChildCounts = BuildExpectedChildCounts(prefab->Nodes());
    SceneEntities entities = scene.Entities();
    bool topologyClean = true;
    for (const ScenePrefabInstanceHandle handle : handles) {
        if (state.prefabInstances.TopologyDirty(handle)) {
            topologyClean = false;
            break;
        }
    }
    if (!topologyClean) {
        for (const ScenePrefabInstanceRecord* instance : instances) {
            if (!InstanceHasExpectedTopology(entities, state, *prefab, *instance, expectedChildCounts)) {
                return BatchTemplateRevertResult::NotEligible;
            }
        }
    }

    const std::span<const ScenePrefabNodeDesc> nodes = prefab->Nodes();
    bool canUseDirtyNodes = true;
    for (const ScenePrefabInstanceHandle handle : handles) {
        const std::span<const std::uint32_t> dirtyNodes = state.prefabInstances.DirtyNodes(handle);
        if (dirtyNodes.empty()) {
            canUseDirtyNodes = false;
            break;
        }
        for (const std::uint32_t nodeIndex : dirtyNodes) {
            if (nodeIndex >= nodes.size()) {
                canUseDirtyNodes = false;
                break;
            }
        }
        if (!canUseDirtyNodes) {
            break;
        }
    }

    ScenePrefabNodeStateWriterContext writerContext{ scene, state };
    for (std::size_t instanceIndex = 0U; instanceIndex < instances.size(); ++instanceIndex) {
        ScenePrefabInstanceRecord* instance = instances[instanceIndex];
        const std::span<const SceneObject> objects = instance->Objects();
        if (canUseDirtyNodes) {
            const ScenePrefabInstanceHandle handle = handles[instanceIndex];
            const std::span<const std::uint32_t> dirtyNodes = state.prefabInstances.DirtyNodes(handle);
            for (const std::uint32_t nodeIndex : dirtyNodes) {
                ScenePrefabNodeStateWriter::Write(
                    writerContext,
                    objects[nodeIndex],
                    ScenePrefabInstanceTopology::ExpectedParent(nodes[nodeIndex], *instance),
                    nodes[nodeIndex]);
            }
            state.prefabInstances.ClearDirtyNodes(handle);
        } else {
            for (std::uint32_t nodeIndex = 0U; nodeIndex < static_cast<std::uint32_t>(nodes.size()); ++nodeIndex) {
                ScenePrefabNodeStateWriter::Write(
                    writerContext,
                    objects[nodeIndex],
                    ScenePrefabInstanceTopology::ExpectedParent(nodes[nodeIndex], *instance),
                    nodes[nodeIndex]);
            }
            state.prefabInstances.ClearDirtyNodes(handles[instanceIndex]);
        }
    }
    return BatchTemplateRevertResult::Reverted;
}

[[nodiscard]] BatchTemplateApplyResult TryApplyIdenticalTemplateBatchFast(
    Scene& scene,
    SceneState& state,
    ScenePrefabRecord& record,
    ScenePrefabHandle prefabHandle,
    std::span<ScenePrefabInstanceRecord* const> instances,
    std::span<const ScenePrefabInstanceHandle> instanceHandles,
    std::span<const ScenePrefabInstanceHandle> sortedResolvedHandles,
    std::uint64_t beforeHash) {
    if (instances.empty() || record.prefab.Empty()) {
        return BatchTemplateApplyResult::NotEligible;
    }

    ScenePrefab candidate = record.prefab;
    ScenePrefabInstanceRecord candidateInstance = *instances.front();
    if (!ScenePrefabOverrideApplier::Apply(scene, candidate, candidateInstance)) {
        return BatchTemplateApplyResult::NotEligible;
    }
    if (!SameObjects(instances.front()->Objects(), candidateInstance.Objects())) {
        return BatchTemplateApplyResult::NotEligible;
    }

    const std::uint64_t candidateHash = ScenePrefabHasher::Hash(candidate);
    if (candidateHash == beforeHash) {
        for (const ScenePrefabInstanceHandle handle : sortedResolvedHandles) {
            state.prefabInstances.ClearDirtyNodes(handle);
        }
        return BatchTemplateApplyResult::Applied;
    }

    if (candidate.NodeCount() == record.prefab.NodeCount()) {
        const std::vector<std::uint32_t> candidateChildCounts = BuildExpectedChildCounts(candidate.Nodes());
        const LivePrefabComponentReaders componentReaders = BuildLivePrefabComponentReaders(scene.Components());
        SceneTransforms transforms = scene.Transforms();
        SceneEntities entities = scene.Entities();
        bool topologyClean = instanceHandles.size() == instances.size();
        if (topologyClean) {
            for (const ScenePrefabInstanceHandle handle : instanceHandles) {
                if (state.prefabInstances.TopologyDirty(handle)) {
                    topologyClean = false;
                    break;
                }
            }
        }
        bool canUseDirtyNodes = instanceHandles.size() == instances.size();
        std::span<const std::uint32_t> candidateDirtyNodes;
        if (canUseDirtyNodes) {
            candidateDirtyNodes = state.prefabInstances.DirtyNodes(instanceHandles.front());
            for (const ScenePrefabInstanceHandle handle : instanceHandles) {
                const std::span<const std::uint32_t> dirtyNodes = state.prefabInstances.DirtyNodes(handle);
                if (dirtyNodes.empty()) {
                    canUseDirtyNodes = false;
                    break;
                }
                if (!DirtyNodesContainAll(dirtyNodes, candidateDirtyNodes)) {
                    canUseDirtyNodes = false;
                    break;
                }
                for (const std::uint32_t nodeIndex : dirtyNodes) {
                    if (nodeIndex >= candidate.NodeCount()) {
                        canUseDirtyNodes = false;
                        break;
                    }
                }
                if (!canUseDirtyNodes) {
                    break;
                }
            }
        }
        for (std::size_t index = 1U; index < instances.size(); ++index) {
            const bool matches = canUseDirtyNodes
                ? ((topologyClean || InstanceHasExpectedTopology(entities, state, candidate, *instances[index], candidateChildCounts))
                    && InstanceDirtyNodesMatchCandidateTemplate(
                        state,
                        componentReaders,
                        transforms,
                        entities,
                        candidate,
                        *instances[index],
                        candidateChildCounts,
                        state.prefabInstances.DirtyNodes(instanceHandles[index])))
                : InstanceMatchesCandidateTemplate(state, componentReaders, transforms, entities, candidate, *instances[index], candidateChildCounts);
            if (!matches) {
                return BatchTemplateApplyResult::NotEligible;
            }
        }
    } else {
        const std::vector<std::uint32_t> expectedChildCounts = BuildExpectedChildCounts(record.prefab.Nodes());
        for (std::size_t index = 1U; index < instances.size(); ++index) {
            const std::optional<std::uint64_t> fingerprint = FingerprintInstanceAsTemplate(scene, state, record.prefab, *instances[index], expectedChildCounts);
            if (!fingerprint.has_value() || *fingerprint != candidateHash) {
                return BatchTemplateApplyResult::NotEligible;
            }
        }
    }

    record.prefab = std::move(candidate);
    state.prefabs.RefreshContentHash(prefabHandle);
    state.prefabs.RefreshDerivedPrefabs(prefabHandle);
    ScenePrefab resolved = ScenePrefabNestedResolver::Resolve(state.prefabs, record.prefab);
    auto sharedResolvedPrefab = std::make_shared<ScenePrefab>(std::move(resolved));
    std::shared_ptr<const std::vector<std::uint64_t>> sharedNodeIds = BuildSharedNodeIds(*sharedResolvedPrefab);

    bool coversEveryDirectInstance = false;
    if (state.prefabs.VariantChildrenOf(prefabHandle).empty()) {
        coversEveryDirectInstance = state.prefabInstances.ContainsExactlyPrefabHandles(prefabHandle, sortedResolvedHandles);
    }

    for (ScenePrefabInstanceRecord* instance : instances) {
        instance->SetSharedResolvedPrefab(sharedResolvedPrefab, sharedNodeIds);
    }
    for (const ScenePrefabInstanceHandle handle : sortedResolvedHandles) {
        state.prefabInstances.ClearDirtyNodes(handle);
    }
    if (!coversEveryDirectInstance) {
        static_cast<void>(ScenePrefabInstanceSynchronizer::Refresh(scene, prefabHandle));
    }
    return BatchTemplateApplyResult::Applied;
}

[[nodiscard]] BatchTemplateApplyResult TryApplyIdenticalTemplateBatch(Scene& scene, SceneState& state, std::span<const ScenePrefabInstanceHandle> handles) {
    if (handles.size() < 2U) {
        return BatchTemplateApplyResult::NotEligible;
    }

    std::vector<ScenePrefabInstanceRecord*> instances;
    instances.reserve(handles.size());

    ScenePrefabHandle prefabHandle;
    for (const ScenePrefabInstanceHandle handle : handles) {
        ScenePrefabInstanceRecord* instance = state.prefabInstances.FindMutable(handle);
        if (instance == nullptr) {
            return BatchTemplateApplyResult::NotEligible;
        }
        if (!prefabHandle.IsValid()) {
            prefabHandle = instance->prefab;
        } else if (instance->prefab != prefabHandle) {
            return BatchTemplateApplyResult::NotEligible;
        }
        instances.push_back(instance);
    }

    std::vector<ScenePrefabInstanceHandle> sortedHandleScratch;
    std::span<const ScenePrefabInstanceHandle> sortedResolvedHandles = handles;
    if (!std::ranges::is_sorted(handles)) {
        sortedHandleScratch.assign(handles.begin(), handles.end());
        std::ranges::sort(sortedHandleScratch);
        sortedResolvedHandles = std::span<const ScenePrefabInstanceHandle>{ sortedHandleScratch.data(), sortedHandleScratch.size() };
    }
    if (std::ranges::adjacent_find(sortedResolvedHandles) != sortedResolvedHandles.end()) {
        return BatchTemplateApplyResult::NotEligible;
    }

    ScenePrefabRecord* record = state.prefabs.FindMutableRecord(prefabHandle);
    if (record == nullptr || record->kind != ScenePrefabRecordKind::Template) {
        return BatchTemplateApplyResult::NotEligible;
    }

    const std::uint64_t beforeHash = ScenePrefabHasher::Hash(record->prefab);
    if (TryApplyIdenticalTemplateBatchFast(
            scene,
            state,
            *record,
            prefabHandle,
            std::span<ScenePrefabInstanceRecord* const>{ instances.data(), instances.size() },
            handles,
            sortedResolvedHandles,
            beforeHash)
        == BatchTemplateApplyResult::Applied) {
        return BatchTemplateApplyResult::Applied;
    }

    std::uint64_t candidateHash = 0U;
    bool hasCandidate = false;
    ScenePrefab candidatePrefab;
    std::vector<std::vector<SceneObject>> updatedObjects;
    std::vector<std::vector<SceneObject>> oldObjects;
    updatedObjects.reserve(instances.size());
    oldObjects.reserve(instances.size());

    for (const ScenePrefabInstanceRecord* instance : instances) {
        ScenePrefab candidate = record->prefab;
        ScenePrefabInstanceRecord candidateInstance = *instance;
        if (!ScenePrefabOverrideApplier::Apply(scene, candidate, candidateInstance)) {
            return BatchTemplateApplyResult::NotEligible;
        }

        const std::uint64_t hash = ScenePrefabHasher::Hash(candidate);
        if (!hasCandidate) {
            candidateHash = hash;
            hasCandidate = true;
            candidatePrefab = std::move(candidate);
        } else if (hash != candidateHash) {
            return BatchTemplateApplyResult::NotEligible;
        }

        const std::span<const SceneObject> previousObjects = instance->Objects();
        oldObjects.emplace_back(previousObjects.begin(), previousObjects.end());
        const std::span<const SceneObject> objects = candidateInstance.Objects();
        updatedObjects.emplace_back(objects.begin(), objects.end());
    }

    if (candidateHash == beforeHash) {
        for (const ScenePrefabInstanceHandle handle : sortedResolvedHandles) {
            state.prefabInstances.ClearDirtyNodes(handle);
        }
        return BatchTemplateApplyResult::Applied;
    }

    record->prefab = std::move(candidatePrefab);
    for (std::size_t index = 0; index < instances.size(); ++index) {
        instances[index]->SetObjects(std::move(updatedObjects[index]));
    }

    state.prefabs.RefreshContentHash(prefabHandle);
    state.prefabs.RefreshDerivedPrefabs(prefabHandle);
    ScenePrefab resolved = ScenePrefabNestedResolver::Resolve(state.prefabs, record->prefab);
    auto sharedResolvedPrefab = std::make_shared<ScenePrefab>(std::move(resolved));
    std::shared_ptr<const std::vector<std::uint64_t>> sharedNodeIds = BuildSharedNodeIds(*sharedResolvedPrefab);

    bool coversEveryDirectInstance = false;
    if (state.prefabs.VariantChildrenOf(prefabHandle).empty()) {
        coversEveryDirectInstance = state.prefabInstances.ContainsExactlyPrefabHandles(prefabHandle, sortedResolvedHandles);
    }

    if (coversEveryDirectInstance) {
        for (std::size_t index = 0; index < instances.size(); ++index) {
            instances[index]->SetSharedResolvedPrefab(sharedResolvedPrefab, sharedNodeIds);
            state.prefabInstances.ReindexObjects(handles[index], oldObjects[index]);
            state.prefabInstances.ClearDirtyNodes(handles[index]);
        }
        return BatchTemplateApplyResult::Applied;
    }

    for (ScenePrefabInstanceRecord* instance : instances) {
        instance->SetSharedResolvedPrefab(sharedResolvedPrefab, sharedNodeIds);
    }
    for (const ScenePrefabInstanceHandle handle : sortedResolvedHandles) {
        state.prefabInstances.ClearDirtyNodes(handle);
    }
    static_cast<void>(ScenePrefabInstanceSynchronizer::Refresh(scene, prefabHandle));
    return BatchTemplateApplyResult::Applied;
}

} // namespace

bool ScenePrefabOverrideMutationService::Revert(Scene& scene, ScenePrefabInstanceHandle handle) {
    SceneState& state = SceneAccess::State(scene);
    ScenePrefabInstanceRecord* instance = state.prefabInstances.FindMutable(handle);
    if (instance == nullptr) {
        return false;
    }

    const ScenePrefab* prefab = ScenePrefabOverrideTargetResolver::ResolveReadPrefab(state, *instance);
    if (prefab == nullptr || !ScenePrefabOverrideReverter::Revert(scene, *prefab, *instance)) {
        return false;
    }
    state.prefabInstances.ClearDirtyNodes(handle);
    return true;
}

bool ScenePrefabOverrideMutationService::Revert(Scene& scene, std::span<const ScenePrefabInstanceHandle> handles) {
    SceneState& state = SceneAccess::State(scene);
    if (TryRevertSamePrefabBatchFast(scene, state, handles) == BatchTemplateRevertResult::Reverted) {
        return true;
    }

    for (const ScenePrefabInstanceHandle handle : handles) {
        ScenePrefabInstanceRecord* instance = state.prefabInstances.FindMutable(handle);
        if (instance == nullptr) {
            return false;
        }

        const ScenePrefab* prefab = ScenePrefabOverrideTargetResolver::ResolveReadPrefab(state, *instance);
        if (prefab == nullptr || !ScenePrefabOverrideReverter::Revert(scene, *prefab, *instance)) {
            return false;
        }
        state.prefabInstances.ClearDirtyNodes(handle);
    }
    return true;
}

bool ScenePrefabOverrideMutationService::Apply(Scene& scene, ScenePrefabInstanceHandle handle) {
    SceneState& state = SceneAccess::State(scene);
    ScenePrefabInstanceRecord* instance = state.prefabInstances.FindMutable(handle);
    if (instance == nullptr) {
        return false;
    }

    ScenePrefabRecord* record = state.prefabs.FindMutableRecord(instance->prefab);
    if (record == nullptr) {
        return false;
    }
    if (record->kind == ScenePrefabRecordKind::Variant) {
        if (!ScenePrefabVariantOverrideService::ApplyAll(scene, state.prefabs, *instance, *record)) {
            return false;
        }
        state.prefabInstances.ClearDirtyNodes(handle);
        return true;
    }

    if (!ScenePrefabTemplateOverrideService::ApplyAll(scene, state.prefabs, *instance, *record)) {
        return false;
    }
    state.prefabInstances.ClearDirtyNodes(handle);
    return true;
}

bool ScenePrefabOverrideMutationService::Apply(Scene& scene, std::span<const ScenePrefabInstanceHandle> handles) {
    SceneState& state = SceneAccess::State(scene);
    if (TryApplyIdenticalTemplateBatch(scene, state, handles) == BatchTemplateApplyResult::Applied) {
        return true;
    }

    for (const ScenePrefabInstanceHandle handle : handles) {
        if (!Apply(scene, handle)) {
            return false;
        }
    }
    return true;
}

bool ScenePrefabOverrideMutationService::RevertProperty(Scene& scene, ScenePrefabInstanceHandle handle, std::uint32_t nodeIndex, std::string_view propertyPath) {
    return ScenePrefabOverridePropertyMutationService::Revert(scene, handle, nodeIndex, propertyPath);
}

bool ScenePrefabOverrideMutationService::ApplyProperty(Scene& scene, ScenePrefabInstanceHandle handle, std::uint32_t nodeIndex, std::string_view propertyPath) {
    return ScenePrefabOverridePropertyMutationService::Apply(scene, handle, nodeIndex, propertyPath);
}

} // namespace kb::scene
