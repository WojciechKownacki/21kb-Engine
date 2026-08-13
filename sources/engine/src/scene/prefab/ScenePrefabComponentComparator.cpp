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
        && lhs.trigger == rhs.trigger
        && lhs.friction == rhs.friction
        && lhs.restitution == rhs.restitution
        && lhs.layer == rhs.layer;
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
        && lhs.dopplerFactor == rhs.dopplerFactor
        && AudioSourceOutputBus(lhs) == AudioSourceOutputBus(rhs);
}

[[nodiscard]] bool Equal(const AudioListenerComponent& lhs, const AudioListenerComponent& rhs) noexcept {
    return lhs.priority == rhs.priority && lhs.localUser == rhs.localUser && lhs.primary == rhs.primary
        && lhs.enabled == rhs.enabled;
}

[[nodiscard]] bool Equal(const Animator& lhs, const Animator& rhs) noexcept {
    return lhs.controllerAssetId == rhs.controllerAssetId &&
        lhs.speed == rhs.speed && lhs.enabled == rhs.enabled &&
        lhs.rootMotionOwner == rhs.rootMotionOwner;
}

[[nodiscard]] bool Equal(const UIDocumentComponent& lhs, const UIDocumentComponent& rhs) noexcept {
    return lhs.documentAssetId == rhs.documentAssetId && lhs.enabled == rhs.enabled;
}

[[nodiscard]] bool Equal(const AuxFrameComponent& lhs, const AuxFrameComponent& rhs) noexcept {
    return lhs.mode == rhs.mode && lhs.imageTargetId == rhs.imageTargetId && lhs.width == rhs.width && lhs.height == rhs.height &&
        Equal(lhs.mirrorPlaneNormal, rhs.mirrorPlaneNormal) && lhs.mirrorPlaneOffset == rhs.mirrorPlaneOffset && lhs.enabled == rhs.enabled;
}
[[nodiscard]] bool Equal(const GeometrySwarmComponent& lhs, const GeometrySwarmComponent& rhs) noexcept {
    return lhs.meshAssetId == rhs.meshAssetId && lhs.materialAssetId == rhs.materialAssetId && lhs.instanceCount == rhs.instanceCount &&
        lhs.columns == rhs.columns && lhs.rows == rhs.rows && lhs.layers == rhs.layers && Equal(lhs.spacing, rhs.spacing) &&
        lhs.instanceScale == rhs.instanceScale && lhs.layer == rhs.layer && lhs.castsShadow == rhs.castsShadow &&
        lhs.receivesShadow == rhs.receivesShadow && lhs.enabled == rhs.enabled;
}
[[nodiscard]] bool Equal(const SurfaceCastComponent& lhs, const SurfaceCastComponent& rhs) noexcept {
    return lhs.materialAssetId == rhs.materialAssetId && lhs.receiverLayerMask == rhs.receiverLayerMask &&
        lhs.order == rhs.order && lhs.content == rhs.content && lhs.enabled == rhs.enabled;
}
[[nodiscard]] bool Equal(const FacingPanelComponent& lhs, const FacingPanelComponent& rhs) noexcept {
    return lhs.mode == rhs.mode && Equal(lhs.targetPoint, rhs.targetPoint) && Equal(lhs.axis, rhs.axis) && Equal(lhs.up, rhs.up) && lhs.enabled == rhs.enabled;
}
[[nodiscard]] bool Equal(const SpaceStrokeComponent& lhs, const SpaceStrokeComponent& rhs) noexcept {
    return lhs.meshAssetId == rhs.meshAssetId && lhs.materialAssetId == rhs.materialAssetId && lhs.mode == rhs.mode &&
        lhs.width == rhs.width && lhs.cableSag == rhs.cableSag && lhs.splineSegments == rhs.splineSegments &&
        lhs.layer == rhs.layer && lhs.castsShadow == rhs.castsShadow && lhs.receivesShadow == rhs.receivesShadow && lhs.enabled == rhs.enabled;
}
[[nodiscard]] bool Equal(const HistoryRibbonComponent& lhs, const HistoryRibbonComponent& rhs) noexcept {
    return lhs.meshAssetId == rhs.meshAssetId && lhs.materialAssetId == rhs.materialAssetId &&
        lhs.lifetimeSeconds == rhs.lifetimeSeconds && lhs.width == rhs.width &&
        lhs.sampleIntervalSeconds == rhs.sampleIntervalSeconds && lhs.layer == rhs.layer &&
        lhs.castsShadow == rhs.castsShadow && lhs.receivesShadow == rhs.receivesShadow && lhs.enabled == rhs.enabled;
}

[[nodiscard]] bool Equal(const ParticleEffectComponent& lhs, const ParticleEffectComponent& rhs) noexcept {
    return lhs.effectAssetId == rhs.effectAssetId && lhs.deterministicSeed == rhs.deterministicSeed &&
        lhs.rateMultiplier == rhs.rateMultiplier && lhs.maxParticlesOverride == rhs.maxParticlesOverride &&
        lhs.ownerDeathPolicy == rhs.ownerDeathPolicy && lhs.enabled == rhs.enabled && lhs.autoPlay == rhs.autoPlay &&
        lhs.followTransform == rhs.followTransform && lhs.restartOnActivate == rhs.restartOnActivate;
}
[[nodiscard]] bool Equal(const SkeletonBindingComponent& lhs, const SkeletonBindingComponent& rhs) noexcept {
    return lhs.skeletonAssetId == rhs.skeletonAssetId &&
        lhs.skeletonCompatibilitySignature == rhs.skeletonCompatibilitySignature && lhs.enabled == rhs.enabled;
}
[[nodiscard]] bool Equal(const MotionSkeletonRuleComponent& lhs, const MotionSkeletonRuleComponent& rhs) noexcept {
    return lhs.kind == rhs.kind && lhs.constrainedBoneId == rhs.constrainedBoneId &&
        lhs.midBoneId == rhs.midBoneId && lhs.tipBoneId == rhs.tipBoneId &&
        lhs.sourceBoneId == rhs.sourceBoneId &&
        MotionSkeletonRuleTargetText(lhs) == MotionSkeletonRuleTargetText(rhs) &&
        MotionSkeletonRulePoleTargetText(lhs) == MotionSkeletonRulePoleTargetText(rhs) &&
        lhs.axis.x == rhs.axis.x && lhs.axis.y == rhs.axis.y && lhs.axis.z == rhs.axis.z &&
        lhs.minAngleDegrees == rhs.minAngleDegrees && lhs.maxAngleDegrees == rhs.maxAngleDegrees &&
        lhs.halfLifeSeconds == rhs.halfLifeSeconds && lhs.weight == rhs.weight && lhs.enabled == rhs.enabled;
}
[[nodiscard]] bool Equal(const DrawD3DeformedGeometryComponent& lhs, const DrawD3DeformedGeometryComponent& rhs) noexcept {
    return lhs.skeletalMeshAssetId == rhs.skeletalMeshAssetId &&
        lhs.materialSlotAssetIds == rhs.materialSlotAssetIds &&
        lhs.materialSlotOverrideCount == rhs.materialSlotOverrideCount &&
        lhs.poseSource == rhs.poseSource && lhs.lodBias == rhs.lodBias && lhs.lodEnabled == rhs.lodEnabled &&
        lhs.fixedBounds == rhs.fixedBounds && lhs.castsShadow == rhs.castsShadow &&
        lhs.receivesShadow == rhs.receivesShadow && lhs.layer == rhs.layer && lhs.enabled == rhs.enabled;
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
    if (!EqualOptionalComponent(components.Animators().TryGet(entity), expected.animator)) {
        flags |= ScenePrefabOverrideFlag::Animator;
    }
    if (!EqualOptionalComponent(components.SkeletonBindings().TryGet(entity), expected.skeletonBinding)) {
        flags |= ScenePrefabOverrideFlag::SkeletonBinding;
    }
    if (!EqualOptionalComponent(components.MotionSkeletonRules().TryGet(entity), expected.motionSkeletonRule)) {
        flags |= ScenePrefabOverrideFlag::MotionSkeletonRule;
    }
    if (!EqualOptionalComponent(components.DeformedGeometries().TryGet(entity), expected.deformedGeometry)) {
        flags |= ScenePrefabOverrideFlag::DeformedGeometry;
    }
    if (!EqualOptionalComponent(components.UIDocuments().TryGet(entity), expected.uiDocument)) {
        flags |= ScenePrefabOverrideFlag::UIDocument;
    }
    if (!EqualOptionalComponent(components.AuxFrames().TryGet(entity), expected.auxFrame)) {
        flags |= ScenePrefabOverrideFlag::AuxFrame;
    }
    if (!EqualOptionalComponent(components.GeometrySwarms().TryGet(entity), expected.geometrySwarm)) flags |= ScenePrefabOverrideFlag::GeometrySwarm;
    if (!EqualOptionalComponent(components.SurfaceCasts().TryGet(entity), expected.surfaceCast)) flags |= ScenePrefabOverrideFlag::SurfaceCast;
    if (!EqualOptionalComponent(components.FacingPanels().TryGet(entity), expected.facingPanel)) flags |= ScenePrefabOverrideFlag::FacingPanel;
    if (!EqualOptionalComponent(components.SpaceStrokes().TryGet(entity), expected.spaceStroke)) flags |= ScenePrefabOverrideFlag::SpaceStroke;
    if (!EqualOptionalComponent(components.HistoryRibbons().TryGet(entity), expected.historyRibbon)) flags |= ScenePrefabOverrideFlag::HistoryRibbon;
    if (!EqualOptionalComponent(components.ParticleEffects().TryGet(entity), expected.particleEffect)) flags |= ScenePrefabOverrideFlag::ParticleEffect;
    const LensEchoComponent* actualLensEcho = components.LensEchoes().TryGet(entity);
    if (!expected.lensEcho.has_value()) {
        if (actualLensEcho != nullptr) flags |= ScenePrefabOverrideFlag::LensEcho;
    } else if (actualLensEcho == nullptr || actualLensEcho->profileMaterialAssetId != expected.lensEcho->profileMaterialAssetId ||
        actualLensEcho->intensity != expected.lensEcho->intensity || actualLensEcho->size != expected.lensEcho->size ||
        actualLensEcho->layer != expected.lensEcho->layer || actualLensEcho->occlusionRule != expected.lensEcho->occlusionRule ||
        actualLensEcho->enabled != expected.lensEcho->enabled) {
        flags |= ScenePrefabOverrideFlag::LensEcho;
    }
    return flags;
}

} // namespace kb::scene
