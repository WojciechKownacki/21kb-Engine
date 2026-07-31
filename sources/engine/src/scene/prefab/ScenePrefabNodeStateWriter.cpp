#include "scene/prefab/ScenePrefabNodeStateWriter.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAudioListenerComponents.hpp"
#include "engine/scene/SceneAudioSourceComponents.hpp"
#include "engine/scene/SceneCameraComponents.hpp"
#include "engine/scene/SceneCharacterControllerComponents.hpp"
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
    return lhs.mode == rhs.mode && lhs.mask == rhs.mask && lhs.visible == rhs.visible;
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

[[nodiscard]] bool Equals(const CharacterControllerComponent& lhs, const CharacterControllerComponent& rhs) noexcept {
    return Equals(lhs.center, rhs.center) && lhs.radius == rhs.radius && lhs.height == rhs.height
        && lhs.slopeLimitDegrees == rhs.slopeLimitDegrees && lhs.stepOffset == rhs.stepOffset
        && lhs.gravityScale == rhs.gravityScale && lhs.useGravity == rhs.useGravity;
}

[[nodiscard]] bool Equals(const TagsComponent& lhs, const TagsComponent& rhs) noexcept {
    return TagsText(lhs) == TagsText(rhs);
}

[[nodiscard]] bool Equals(const RegionShapeComponent& lhs, const RegionShapeComponent& rhs) noexcept {
    return lhs.kind == rhs.kind && Equals(lhs.center, rhs.center) && Equals(lhs.size, rhs.size)
        && lhs.radius == rhs.radius && lhs.height == rhs.height && lhs.enabled == rhs.enabled;
}

[[nodiscard]] bool Equals(const GuideCurveComponent& lhs, const GuideCurveComponent& rhs) noexcept {
    if (lhs.controlPointCount != rhs.controlPointCount || lhs.interpolation != rhs.interpolation || lhs.closed != rhs.closed || lhs.enabled != rhs.enabled) return false;
    for (std::uint32_t index = 0U; index < lhs.controlPointCount; ++index) if (!Equals(lhs.controlPoints[index], rhs.controlPoints[index])) return false;
    return true;
}

[[nodiscard]] bool Equals(const ContentInstanceComponent& lhs, const ContentInstanceComponent& rhs) noexcept {
    return lhs.assetId == rhs.assetId && lhs.kind == rhs.kind && lhs.lifetime == rhs.lifetime && lhs.active == rhs.active;
}

[[nodiscard]] bool Equals(const StreamFocusComponent& lhs, const StreamFocusComponent& rhs) noexcept {
    return lhs.innerRadius == rhs.innerRadius && lhs.outerRadius == rhs.outerRadius && lhs.priority == rhs.priority
        && lhs.loadMask == rhs.loadMask && lhs.enabled == rhs.enabled;
}

[[nodiscard]] bool Equals(const WorldBackdropComponent& lhs, const WorldBackdropComponent& rhs) noexcept {
    return lhs.mode == rhs.mode && Equals(lhs.color, rhs.color) && Equals(lhs.horizonColor, rhs.horizonColor) &&
        Equals(lhs.zenithColor, rhs.zenithColor) && lhs.environmentAssetId == rhs.environmentAssetId &&
        lhs.horizonHeight == rhs.horizonHeight && lhs.gradientExponent == rhs.gradientExponent &&
        lhs.priority == rhs.priority && lhs.enabled == rhs.enabled;
}

[[nodiscard]] bool Equals(const AmbientRadianceComponent& lhs, const AmbientRadianceComponent& rhs) noexcept {
    return lhs.mode == rhs.mode && Equals(lhs.color, rhs.color) && Equals(lhs.horizonColor, rhs.horizonColor) &&
        Equals(lhs.zenithColor, rhs.zenithColor) && lhs.environmentAssetId == rhs.environmentAssetId &&
        lhs.intensity == rhs.intensity && lhs.diffuseIntensity == rhs.diffuseIntensity &&
        lhs.specularIntensity == rhs.specularIntensity && lhs.priority == rhs.priority && lhs.enabled == rhs.enabled;
}

[[nodiscard]] bool Equals(const SceneDetailSwitchComponent& lhs, const SceneDetailSwitchComponent& rhs) noexcept {
    return lhs.groupId == rhs.groupId && lhs.minimumLod == rhs.minimumLod && lhs.maximumLod == rhs.maximumLod &&
        lhs.promoteCoverage == rhs.promoteCoverage && lhs.demoteCoverage == rhs.demoteCoverage && lhs.enabled == rhs.enabled;
}

[[nodiscard]] bool Equals(const SceneVisibilityBlockerComponent& lhs, const SceneVisibilityBlockerComponent& rhs) noexcept {
    return Equals(lhs.localCenter, rhs.localCenter) && Equals(lhs.size, rhs.size) && lhs.enabled == rhs.enabled;
}

[[nodiscard]] bool Equals(const VisibilityCellComponent& lhs, const VisibilityCellComponent& rhs) noexcept {
    return lhs.membershipMask == rhs.membershipMask && lhs.membership == rhs.membership && lhs.visibilityOverride == rhs.visibilityOverride && lhs.enabled == rhs.enabled;
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

[[nodiscard]] bool Equals(const Animator& lhs, const Animator& rhs) noexcept {
    return lhs.controllerAssetId == rhs.controllerAssetId &&
        lhs.speed == rhs.speed && lhs.enabled == rhs.enabled &&
        lhs.rootMotionOwner == rhs.rootMotionOwner;
}

[[nodiscard]] bool Equals(const UIDocumentComponent& lhs, const UIDocumentComponent& rhs) noexcept {
    return lhs.documentAssetId == rhs.documentAssetId && lhs.enabled == rhs.enabled;
}

[[nodiscard]] bool Equals(const NavAgent& lhs, const NavAgent& rhs) noexcept {
    return lhs.radius == rhs.radius && lhs.height == rhs.height && lhs.maxSpeed == rhs.maxSpeed && lhs.acceleration == rhs.acceleration &&
        lhs.angularSpeedDegrees == rhs.angularSpeedDegrees && lhs.stoppingDistance == rhs.stoppingDistance && lhs.areaMask == rhs.areaMask &&
        Equals(lhs.destination, rhs.destination) && Equals(lhs.velocity, rhs.velocity) && lhs.remainingDistance == rhs.remainingDistance &&
        lhs.pathStatus == rhs.pathStatus && lhs.enabled == rhs.enabled;
}

[[nodiscard]] bool Equals(const NavObstacle& lhs, const NavObstacle& rhs) noexcept {
    return lhs.shape == rhs.shape && Equals(lhs.center, rhs.center) && Equals(lhs.size, rhs.size) && lhs.radius == rhs.radius &&
        lhs.height == rhs.height && lhs.area == rhs.area && lhs.carve == rhs.carve && lhs.enabled == rhs.enabled;
}

[[nodiscard]] bool Equals(const AuxFrameComponent& lhs, const AuxFrameComponent& rhs) noexcept {
    return lhs.mode == rhs.mode && lhs.imageTargetId == rhs.imageTargetId && lhs.width == rhs.width && lhs.height == rhs.height &&
        Equals(lhs.mirrorPlaneNormal, rhs.mirrorPlaneNormal) && lhs.mirrorPlaneOffset == rhs.mirrorPlaneOffset && lhs.enabled == rhs.enabled;
}
[[nodiscard]] bool Equals(const GeometrySwarmComponent& lhs, const GeometrySwarmComponent& rhs) noexcept {
    return lhs.meshAssetId == rhs.meshAssetId && lhs.materialAssetId == rhs.materialAssetId && lhs.instanceCount == rhs.instanceCount &&
        lhs.columns == rhs.columns && lhs.rows == rhs.rows && lhs.layers == rhs.layers && Equals(lhs.spacing, rhs.spacing) &&
        lhs.instanceScale == rhs.instanceScale && lhs.layer == rhs.layer && lhs.castsShadow == rhs.castsShadow && lhs.receivesShadow == rhs.receivesShadow && lhs.enabled == rhs.enabled;
}
[[nodiscard]] bool Equals(const SurfaceCastComponent& lhs, const SurfaceCastComponent& rhs) noexcept {
    return lhs.materialAssetId == rhs.materialAssetId && lhs.receiverLayerMask == rhs.receiverLayerMask &&
        lhs.order == rhs.order && lhs.content == rhs.content && lhs.enabled == rhs.enabled;
}
[[nodiscard]] bool Equals(const FacingPanelComponent& lhs, const FacingPanelComponent& rhs) noexcept {
    return lhs.mode == rhs.mode && Equals(lhs.targetPoint, rhs.targetPoint) && Equals(lhs.axis, rhs.axis) && Equals(lhs.up, rhs.up) && lhs.enabled == rhs.enabled;
}
[[nodiscard]] bool Equals(const SpaceStrokeComponent& lhs, const SpaceStrokeComponent& rhs) noexcept {
    return lhs.meshAssetId == rhs.meshAssetId && lhs.materialAssetId == rhs.materialAssetId && lhs.mode == rhs.mode && lhs.width == rhs.width &&
        lhs.cableSag == rhs.cableSag && lhs.splineSegments == rhs.splineSegments && lhs.layer == rhs.layer && lhs.castsShadow == rhs.castsShadow &&
        lhs.receivesShadow == rhs.receivesShadow && lhs.enabled == rhs.enabled;
}

[[nodiscard]] bool Equals(const HistoryRibbonComponent& lhs, const HistoryRibbonComponent& rhs) noexcept {
    return lhs.meshAssetId == rhs.meshAssetId && lhs.materialAssetId == rhs.materialAssetId &&
        lhs.lifetimeSeconds == rhs.lifetimeSeconds && lhs.width == rhs.width &&
        lhs.sampleIntervalSeconds == rhs.sampleIntervalSeconds && lhs.layer == rhs.layer &&
        lhs.castsShadow == rhs.castsShadow && lhs.receivesShadow == rhs.receivesShadow && lhs.enabled == rhs.enabled;
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
    , characterControllers(scene.Components().CharacterControllers())
    , tags(scene.Components().Tags())
    , regionShapes(scene.Components().RegionShapes())
    , guideCurves(scene.Components().GuideCurves())
    , contentInstances(scene.Components().ContentInstances())
    , streamFocuses(scene.Components().StreamFocuses())
    , worldBackdrops(scene.Components().WorldBackdrops())
    , ambientRadiances(scene.Components().AmbientRadiances())
    , detailSwitches(scene.Components().DetailSwitches())
    , visibilityBlockers(scene.Components().VisibilityBlockers())
    , visibilityCells(scene.Components().VisibilityCells())
    , regionPortals(scene.Components().RegionPortals())
    , auxFrames(scene.Components().AuxFrames())
    , geometrySwarms(scene.Components().GeometrySwarms())
    , surfaceCasts(scene.Components().SurfaceCasts())
    , facingPanels(scene.Components().FacingPanels())
    , spaceStrokes(scene.Components().SpaceStrokes())
    , historyRibbons(scene.Components().HistoryRibbons())
    , lensEchoes(scene.Components().LensEchoes())
    , behaviours(scene.Components().Behaviours())
    , audioSources(scene.Components().AudioSources())
    , audioListeners(scene.Components().AudioListeners())
    , animators(scene.Components().Animators())
    , uiDocuments(scene.Components().UIDocuments())
    , navAgents(scene.Components().NavAgents())
    , navObstacles(scene.Components().NavObstacles()) {
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
    if (!componentMask.available || !componentMask.matches || node.components.characterController.has_value()) {
        WriteOptionalComponent(context.characterControllers, entity, node.components.characterController);
    }
    if (!componentMask.available || !componentMask.matches || node.components.tags.has_value()) {
        WriteOptionalComponent(context.tags, entity, node.components.tags);
    }
    if (!componentMask.available || !componentMask.matches || node.components.regionShape.has_value()) {
        WriteOptionalComponent(context.regionShapes, entity, node.components.regionShape);
    }
    WriteOptionalComponent(context.guideCurves, entity, node.components.guideCurve);
    WriteOptionalComponent(context.contentInstances, entity, node.components.contentInstance);
    WriteOptionalComponent(context.streamFocuses, entity, node.components.streamFocus);
    WriteOptionalComponent(context.worldBackdrops, entity, node.components.worldBackdrop);
    WriteOptionalComponent(context.ambientRadiances, entity, node.components.ambientRadiance);
    WriteOptionalComponent(context.detailSwitches, entity, node.components.detailSwitch);
    WriteOptionalComponent(context.visibilityBlockers, entity, node.components.visibilityBlocker);
    WriteOptionalComponent(context.visibilityCells, entity, node.components.visibilityCell);
    // Portal references are resolved only after all prefab nodes exist; this
    // writer owns the removal case while the synchronizer performs that final
    // graph-wide resolution for present portals.
    if (!node.components.regionPortal.has_value()) context.regionPortals.Remove(entity);
    WriteOptionalComponent(context.auxFrames, entity, node.components.auxFrame);
    WriteOptionalComponent(context.geometrySwarms, entity, node.components.geometrySwarm);
    WriteOptionalComponent(context.surfaceCasts, entity, node.components.surfaceCast);
    WriteOptionalComponent(context.facingPanels, entity, node.components.facingPanel);
    WriteOptionalComponent(context.spaceStrokes, entity, node.components.spaceStroke);
    WriteOptionalComponent(context.historyRibbons, entity, node.components.historyRibbon);
    // Echo source references are resolved after every prefab node exists.
    if (!node.components.lensEcho.has_value()) context.lensEchoes.Remove(entity);
    if (!componentMask.available || !componentMask.matches || node.components.behaviour.has_value()) {
        WriteOptionalComponent(context.behaviours, entity, node.components.behaviour);
    }
    if (!componentMask.available || !componentMask.matches || node.components.audioSource.has_value()) {
        WriteOptionalComponent(context.audioSources, entity, node.components.audioSource);
    }
    if (!componentMask.available || !componentMask.matches || node.components.audioListener.has_value()) {
        WriteOptionalComponent(context.audioListeners, entity, node.components.audioListener);
    }
    if (!componentMask.available || !componentMask.matches || node.components.animator.has_value()) {
        WriteOptionalComponent(context.animators, entity, node.components.animator);
    }
    if (!componentMask.available || !componentMask.matches || node.components.uiDocument.has_value()) {
        WriteOptionalComponent(context.uiDocuments, entity, node.components.uiDocument);
    }
    WriteOptionalComponent(context.navAgents, entity, node.components.navAgent);
    WriteOptionalComponent(context.navObstacles, entity, node.components.navObstacle);
}

} // namespace kb::scene
