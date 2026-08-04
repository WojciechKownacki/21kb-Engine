#include "scene/asset/io/SceneAssetComponentCodec.hpp"

#include "scene/asset/io/SceneAssetPrimitiveCodec.hpp"

#include "scene/asset/io/components/SceneAssetAudioComponentCodec.hpp"
#include "scene/asset/io/components/SceneAssetBehaviourComponentCodec.hpp"
#include "scene/asset/io/components/SceneAssetCameraComponentCodec.hpp"
#include "scene/asset/io/components/SceneAssetInputComponentCodec.hpp"
#include "scene/asset/io/components/SceneAssetPhysicsComponentCodec.hpp"
#include "scene/asset/io/components/SceneAssetRenderComponentCodec.hpp"
#include "scene/asset/io/components/SceneAssetTagsComponentCodec.hpp"

#include <cmath>
#include <stdexcept>

namespace kb::scene {
namespace {

enum SceneNodeComponentBits : std::uint64_t {
      CameraBit = 1ULL << 0U,
    MeshRendererBit = 1U << 1U,
    LightBit = 1U << 2U,
    InputBit = 1U << 3U,
    RigidbodyBit = 1U << 4U,
    ColliderBit = 1U << 5U,
    AudioSourceBit = 1U << 6U,
    AudioListenerBit = 1U << 7U,
    BehaviourBit = 1U << 8U,
    TagsBit = 1U << 9U,
    CharacterControllerBit = 1U << 10U,
    JointBit = 1U << 11U,
    AnimatorBit = 1U << 12U,
    UIDocumentBit = 1U << 13U,
    NavAgentBit = 1U << 14U,
    NavObstacleBit = 1U << 15U,
    RegionShapeBit = 1U << 16U,
      GuideCurveBit = 1U << 17U,
      ContentInstanceBit = 1U << 18U,
      StreamFocusBit = 1U << 19U,
      WorldBackdropBit = 1U << 20U,
      AmbientRadianceBit = 1U << 21U,
      DetailSwitchBit = 1U << 22U,
      VisibilityBlockerBit = 1U << 23U,
      VisibilityCellBit = 1U << 24U,
      RegionPortalBit = 1U << 25U,
      AuxFrameBit = 1U << 26U,
      GeometrySwarmBit = 1U << 27U,
      SurfaceCastBit = 1U << 28U,
      FacingPanelBit = 1U << 29U,
      SpaceStrokeBit = 1U << 30U,
      HistoryRibbonBit = 1ULL << 31U,
      LensEchoBit = 1ULL << 32U,
      SkeletonBindingBit = 1ULL << 33U,
      DeformedGeometryBit = 1ULL << 34U,
};

constexpr std::uint64_t KnownComponentBits = CameraBit |
    MeshRendererBit |
    LightBit |
    InputBit |
    RigidbodyBit |
    ColliderBit |
    AudioSourceBit |
    AudioListenerBit |
    BehaviourBit |
    TagsBit |
    CharacterControllerBit |
    JointBit |
    AnimatorBit |
    UIDocumentBit |
    NavAgentBit |
    NavObstacleBit |
    RegionShapeBit |
    GuideCurveBit |
      ContentInstanceBit | StreamFocusBit | WorldBackdropBit | AmbientRadianceBit | DetailSwitchBit | VisibilityBlockerBit | VisibilityCellBit | RegionPortalBit | AuxFrameBit | GeometrySwarmBit | SurfaceCastBit | FacingPanelBit | SpaceStrokeBit | HistoryRibbonBit | LensEchoBit | SkeletonBindingBit | DeformedGeometryBit;

[[nodiscard]] std::uint64_t ComponentBits(const ScenePrefabNodeComponents& components) noexcept {
    std::uint64_t componentBits = 0;
    const auto include = [&componentBits](bool present, SceneNodeComponentBits bit) noexcept {
        if (present) componentBits |= static_cast<std::uint64_t>(bit);
    };
    include(components.camera.has_value(), CameraBit);
    include(components.meshRenderer.has_value(), MeshRendererBit);
    include(components.light.has_value(), LightBit);
    include(components.input.has_value(), InputBit);
    include(components.rigidbody.has_value(), RigidbodyBit);
    include(components.collider.has_value(), ColliderBit);
    include(components.audioSource.has_value(), AudioSourceBit);
    include(components.audioListener.has_value(), AudioListenerBit);
    include(components.behaviour.has_value(), BehaviourBit);
    include(components.tags.has_value(), TagsBit);
    include(components.characterController.has_value(), CharacterControllerBit);
    include(components.joint.has_value(), JointBit);
    include(components.animator.has_value(), AnimatorBit);
    include(components.uiDocument.has_value(), UIDocumentBit);
    include(components.navAgent.has_value(), NavAgentBit);
    include(components.navObstacle.has_value(), NavObstacleBit);
    include(components.regionShape.has_value(), RegionShapeBit);
    include(components.guideCurve.has_value(), GuideCurveBit);
    include(components.contentInstance.has_value(), ContentInstanceBit);
    include(components.streamFocus.has_value(), StreamFocusBit);
    include(components.worldBackdrop.has_value(), WorldBackdropBit);
    include(components.ambientRadiance.has_value(), AmbientRadianceBit);
    include(components.detailSwitch.has_value(), DetailSwitchBit);
    include(components.visibilityBlocker.has_value(), VisibilityBlockerBit);
    include(components.visibilityCell.has_value(), VisibilityCellBit);
    include(components.regionPortal.has_value(), RegionPortalBit);
    include(components.auxFrame.has_value(), AuxFrameBit);
    include(components.geometrySwarm.has_value(), GeometrySwarmBit);
    include(components.surfaceCast.has_value(), SurfaceCastBit);
    include(components.facingPanel.has_value(), FacingPanelBit);
    include(components.spaceStroke.has_value(), SpaceStrokeBit);
    include(components.historyRibbon.has_value(), HistoryRibbonBit);
    include(components.lensEcho.has_value(), LensEchoBit);
    include(components.skeletonBinding.has_value(), SkeletonBindingBit);
    include(components.deformedGeometry.has_value(), DeformedGeometryBit);
    return componentBits;
}

} // namespace

bool SceneAssetComponentCodec::Read(SceneAssetBinaryIO::ByteReader& input, std::uint32_t fileVersion, ScenePrefabNodeComponents& output) {
    std::uint64_t componentBits = 0U;
    if (fileVersion <= 25U) {
        std::uint32_t legacyComponentBits = 0U;
        if (!input.ReadUInt32(legacyComponentBits)) return false;
        componentBits = legacyComponentBits;
    } else if (!input.ReadUInt64(componentBits)) {
        return false;
    }
    if ((componentBits & ~KnownComponentBits) != 0U) return false;

    if ((componentBits & CameraBit) != 0U) {
        CameraComponent camera;
        if (!SceneAssetCameraComponentCodec::Read(input, camera)) {
            return false;
        }
        output.camera = camera;
    }
    if ((componentBits & MeshRendererBit) != 0U) {
        MeshRendererComponent meshRenderer;
        if (!SceneAssetRenderComponentCodec::ReadMeshRenderer(input, meshRenderer)) {
            return false;
        }
        output.meshRenderer = meshRenderer;
    }
    if ((componentBits & LightBit) != 0U) {
        LightComponent light;
        if (!SceneAssetRenderComponentCodec::ReadLight(input, light)) {
            return false;
        }
        output.light = light;
    }
    if ((componentBits & InputBit) != 0U) {
        InputComponent inputComponent;
        if (!SceneAssetInputComponentCodec::Read(input, inputComponent)) {
            return false;
        }
        output.input = inputComponent;
    }
    if ((componentBits & RigidbodyBit) != 0U) {
        RigidbodyComponent rigidbody;
        if (!SceneAssetPhysicsComponentCodec::ReadRigidbody(input, rigidbody)) {
            return false;
        }
        output.rigidbody = rigidbody;
    }
    if ((componentBits & ColliderBit) != 0U) {
        ColliderComponent collider;
        if (!SceneAssetPhysicsComponentCodec::ReadCollider(input, collider)) {
            return false;
        }
        output.collider = collider;
    }
    if ((componentBits & CharacterControllerBit) != 0U) {
        CharacterControllerComponent characterController;
        if (!SceneAssetPhysicsComponentCodec::ReadCharacterController(input, characterController)) {
            return false;
        }
        output.characterController = characterController;
    }
    if ((componentBits & JointBit) != 0U) {
        if (fileVersion < 4U) {
            return false;
        }
        ScenePrefabJointComponent joint;
        if (!SceneAssetPhysicsComponentCodec::ReadJoint(input, joint)) {
            return false;
        }
        output.joint = joint;
    }
    if ((componentBits & TagsBit) != 0U) {
        TagsComponent tags;
        if (!SceneAssetTagsComponentCodec::Read(input, tags)) {
            return false;
        }
        output.tags = tags;
    }
    if ((componentBits & AudioSourceBit) != 0U) {
        AudioSourceComponent audioSource;
        if (!SceneAssetAudioComponentCodec::ReadSource(input, fileVersion, audioSource)) {
            return false;
        }
        output.audioSource = audioSource;
    }
    if ((componentBits & AudioListenerBit) != 0U) {
        AudioListenerComponent audioListener;
        if (!SceneAssetAudioComponentCodec::ReadListener(input, audioListener)) {
            return false;
        }
        output.audioListener = audioListener;
    }
    if ((componentBits & BehaviourBit) != 0U) {
        BehaviourComponent behaviour;
        if (!SceneAssetBehaviourComponentCodec::Read(input, behaviour)) {
            return false;
        }
        output.behaviour = behaviour;
    }
    if ((componentBits & AnimatorBit) != 0U) {
        if (fileVersion < 5U) return false;
        Animator animator{};
        if (!input.ReadUInt64(animator.controllerAssetId) ||
            !input.ReadFloat(animator.speed) ||
            !input.ReadBool(animator.enabled) ||
            !std::isfinite(animator.speed) || animator.speed < 0.0F) return false;
        if (fileVersion >= 6U) {
            std::uint32_t rootMotionOwner = 0U;
            if (!input.ReadUInt32(rootMotionOwner) ||
                rootMotionOwner > static_cast<std::uint32_t>(AnimatorRootMotionOwner::Rigidbody)) return false;
            animator.rootMotionOwner = static_cast<AnimatorRootMotionOwner>(rootMotionOwner);
        }
        if (fileVersion >= 29U &&
            (!input.ReadFloat(animator.poseUpdateRateHz) ||
             !std::isfinite(animator.poseUpdateRateHz) || animator.poseUpdateRateHz < 0.0F)) return false;
        output.animator = animator;
    }
    if ((componentBits & SkeletonBindingBit) != 0U) {
        if (fileVersion < 28U) return false;
        SkeletonBindingComponent binding{};
        if (!input.ReadUInt64(binding.skeletonAssetId) || !input.ReadUInt64(binding.skeletonCompatibilitySignature) ||
            !input.ReadBool(binding.enabled) || !IsSkeletonBindingComponentPersistable(binding)) return false;
        output.skeletonBinding = binding;
    }
    if ((componentBits & DeformedGeometryBit) != 0U) {
        if (fileVersion < 28U) return false;
        DrawD3DeformedGeometryComponent geometry{};
        std::uint64_t poseSourceStableId = 0U;
        if (!input.ReadUInt64(geometry.skeletalMeshAssetId) || !input.ReadUInt32(geometry.materialSlotOverrideCount) ||
            geometry.materialSlotOverrideCount > kMaxDeformedGeometryMaterialSlotOverrides) return false;
        for (std::uint64_t& material : geometry.materialSlotAssetIds) if (!input.ReadUInt64(material)) return false;
        if (!input.ReadUInt64(poseSourceStableId) || poseSourceStableId != 0U || !input.ReadInt32(geometry.lodBias) ||
            !input.ReadBool(geometry.lodEnabled) || !input.ReadBool(geometry.fixedBounds) || !input.ReadBool(geometry.castsShadow) ||
            !input.ReadBool(geometry.receivesShadow) || !input.ReadUInt32(geometry.layer) || !input.ReadBool(geometry.enabled) ||
            !IsDrawD3DeformedGeometryComponentPersistable(geometry)) return false;
        output.deformedGeometry = geometry;
    }
    if ((componentBits & UIDocumentBit) != 0U) {
        if (fileVersion < 7U) return false;
        UIDocumentComponent uiDocument{};
        if (!input.ReadUInt64(uiDocument.documentAssetId) || !input.ReadBool(uiDocument.enabled)) return false;
        output.uiDocument = uiDocument;
    }
    if ((componentBits & NavAgentBit) != 0U) {
        if (fileVersion < 8U) return false;
        NavAgent agent{};
        std::uint32_t status = 0U;
        if (!input.ReadFloat(agent.radius) || !input.ReadFloat(agent.height) || !input.ReadFloat(agent.maxSpeed) ||
            !input.ReadFloat(agent.acceleration) || !input.ReadFloat(agent.angularSpeedDegrees) || !input.ReadFloat(agent.stoppingDistance) ||
            !input.ReadUInt32(agent.areaMask) || !SceneAssetPrimitiveCodec::ReadVec3(input, agent.destination) ||
            !SceneAssetPrimitiveCodec::ReadVec3(input, agent.velocity) || !input.ReadFloat(agent.remainingDistance) ||
            !input.ReadUInt32(status) || !input.ReadBool(agent.enabled) ||
            !(agent.radius > 0.0F) || !(agent.height > 0.0F) || !(agent.maxSpeed > 0.0F) || !(agent.acceleration >= 0.0F) ||
            !(agent.angularSpeedDegrees >= 0.0F) || !(agent.stoppingDistance >= 0.0F) || !std::isfinite(agent.radius) ||
            !std::isfinite(agent.height) || !std::isfinite(agent.maxSpeed) || !std::isfinite(agent.acceleration) ||
            !std::isfinite(agent.angularSpeedDegrees) || !std::isfinite(agent.stoppingDistance) || !std::isfinite(agent.remainingDistance) ||
            status > static_cast<std::uint32_t>(NavPathStatus::Cancelled)) return false;
        agent.pathStatus = static_cast<NavPathStatus>(status);
        output.navAgent = agent;
    }
    if ((componentBits & NavObstacleBit) != 0U) {
        if (fileVersion < 8U) return false;
        NavObstacle obstacle{};
        std::uint32_t shape = 0U;
        std::uint32_t area = 0U;
        if (!input.ReadUInt32(shape) || !SceneAssetPrimitiveCodec::ReadVec3(input, obstacle.center) ||
            !SceneAssetPrimitiveCodec::ReadVec3(input, obstacle.size) || !input.ReadFloat(obstacle.radius) ||
            !input.ReadFloat(obstacle.height) || !input.ReadUInt32(area) || !input.ReadBool(obstacle.carve) || !input.ReadBool(obstacle.enabled) ||
            shape > static_cast<std::uint32_t>(NavObstacleShape::Cylinder) || area >= kNavAreaCount ||
            !(obstacle.radius > 0.0F) || !(obstacle.height > 0.0F) || !std::isfinite(obstacle.radius) || !std::isfinite(obstacle.height)) return false;
        obstacle.shape = static_cast<NavObstacleShape>(shape);
        obstacle.area = static_cast<NavAreaId>(area);
        output.navObstacle = obstacle;
    }
    if ((componentBits & RegionShapeBit) != 0U) {
        if (fileVersion < 10U) return false;
        RegionShapeComponent regionShape{};
        std::uint32_t kind = 0U;
        if (!input.ReadUInt32(kind) || !SceneAssetPrimitiveCodec::ReadVec3(input, regionShape.center) ||
            !SceneAssetPrimitiveCodec::ReadVec3(input, regionShape.size) || !input.ReadFloat(regionShape.radius) ||
            !input.ReadFloat(regionShape.height) || !input.ReadBool(regionShape.enabled) ||
            kind > static_cast<std::uint32_t>(RegionShapeKind::Capsule) ||
            !std::isfinite(regionShape.center.x) || !std::isfinite(regionShape.center.y) || !std::isfinite(regionShape.center.z) ||
            !std::isfinite(regionShape.size.x) || !std::isfinite(regionShape.size.y) || !std::isfinite(regionShape.size.z) ||
            !(regionShape.radius > 0.0F) || !(regionShape.height > 0.0F) || !std::isfinite(regionShape.radius) || !std::isfinite(regionShape.height)) return false;
        regionShape.kind = static_cast<RegionShapeKind>(kind);
        output.regionShape = regionShape;
    }
    if ((componentBits & GuideCurveBit) != 0U) {
        if (fileVersion < 11U) return false;
        GuideCurveComponent guideCurve{};
        std::uint32_t interpolation = 0U;
        if (!input.ReadUInt32(guideCurve.controlPointCount) || !input.ReadUInt32(interpolation) || !input.ReadBool(guideCurve.closed) || !input.ReadBool(guideCurve.enabled) ||
            !IsGuideCurveControlPointCountValid(guideCurve.controlPointCount) || interpolation > static_cast<std::uint32_t>(GuideCurveInterpolation::CatmullRom)) return false;
        guideCurve.interpolation = static_cast<GuideCurveInterpolation>(interpolation);
        for (std::uint32_t index = 0U; index < guideCurve.controlPointCount; ++index) {
            if (!SceneAssetPrimitiveCodec::ReadVec3(input, guideCurve.controlPoints[index]) || !std::isfinite(guideCurve.controlPoints[index].x) || !std::isfinite(guideCurve.controlPoints[index].y) || !std::isfinite(guideCurve.controlPoints[index].z)) return false;
        }
        output.guideCurve = guideCurve;
    }
    if ((componentBits & ContentInstanceBit) != 0U) {
        if (fileVersion < 12U) return false;
        ContentInstanceComponent content{};
        std::uint32_t kind = 0U;
        std::uint32_t lifetime = 0U;
        if (!input.ReadUInt64(content.assetId) || !input.ReadUInt32(kind) || !input.ReadUInt32(lifetime) || !input.ReadBool(content.active) ||
            kind > static_cast<std::uint32_t>(ContentInstanceKind::WorldFragment) || lifetime > static_cast<std::uint32_t>(ContentInstanceLifetime::Persistent)) return false;
        content.kind = static_cast<ContentInstanceKind>(kind);
        content.lifetime = static_cast<ContentInstanceLifetime>(lifetime);
        output.contentInstance = content;
    }
    if ((componentBits & StreamFocusBit) != 0U) {
        if (fileVersion < 13U) return false;
        StreamFocusComponent focus{};
        std::uint32_t mask = 0U;
        if (!input.ReadFloat(focus.innerRadius) || !input.ReadFloat(focus.outerRadius) || !input.ReadInt32(focus.priority) || !input.ReadUInt32(mask) || !input.ReadBool(focus.enabled)) return false;
        focus.loadMask = static_cast<StreamLoadMask>(mask);
        if (!IsStreamFocusValid(focus)) return false;
        output.streamFocus = focus;
    }
    if ((componentBits & WorldBackdropBit) != 0U) {
        if (fileVersion < 14U) return false;
        WorldBackdropComponent backdrop{};
        std::uint32_t mode = 0U;
        if (!input.ReadUInt32(mode) ||
            !SceneAssetPrimitiveCodec::ReadVec3(input, backdrop.color) ||
            !SceneAssetPrimitiveCodec::ReadVec3(input, backdrop.horizonColor) ||
            !SceneAssetPrimitiveCodec::ReadVec3(input, backdrop.zenithColor) ||
            !input.ReadUInt64(backdrop.environmentAssetId) || !input.ReadFloat(backdrop.horizonHeight) ||
            !input.ReadFloat(backdrop.gradientExponent) || !input.ReadInt32(backdrop.priority) ||
            !input.ReadBool(backdrop.enabled)) return false;
        backdrop.mode = static_cast<WorldBackdropMode>(mode);
        if (!IsWorldBackdropComponentValid(backdrop)) return false;
        output.worldBackdrop = backdrop;
    }
    if ((componentBits & AmbientRadianceBit) != 0U) {
        if (fileVersion < 15U) return false;
        AmbientRadianceComponent ambient{};
        std::uint32_t mode = 0U;
        if (!input.ReadUInt32(mode) ||
            !SceneAssetPrimitiveCodec::ReadVec3(input, ambient.color) ||
            !SceneAssetPrimitiveCodec::ReadVec3(input, ambient.horizonColor) ||
            !SceneAssetPrimitiveCodec::ReadVec3(input, ambient.zenithColor) ||
            !input.ReadUInt64(ambient.environmentAssetId) || !input.ReadFloat(ambient.intensity) ||
            !input.ReadFloat(ambient.diffuseIntensity) || !input.ReadFloat(ambient.specularIntensity) ||
            !input.ReadInt32(ambient.priority) || !input.ReadBool(ambient.enabled)) return false;
        ambient.mode = static_cast<AmbientRadianceMode>(mode);
        if (!IsAmbientRadianceComponentValid(ambient)) return false;
        output.ambientRadiance = ambient;
    }
    if ((componentBits & DetailSwitchBit) != 0U) {
        if (fileVersion < 16U) return false;
        SceneDetailSwitchComponent detail{};
        if (!input.ReadUInt64(detail.groupId) || !input.ReadUInt32(detail.minimumLod) ||
            !input.ReadUInt32(detail.maximumLod) || !input.ReadFloat(detail.promoteCoverage) ||
            !input.ReadFloat(detail.demoteCoverage) || !input.ReadBool(detail.enabled) ||
            !IsSceneDetailSwitchComponentValid(detail)) return false;
        output.detailSwitch = detail;
    }
    if ((componentBits & VisibilityBlockerBit) != 0U) {
        if (fileVersion < 17U) return false;
        SceneVisibilityBlockerComponent blocker{};
        if (!SceneAssetPrimitiveCodec::ReadVec3(input, blocker.localCenter) || !SceneAssetPrimitiveCodec::ReadVec3(input, blocker.size) || !input.ReadBool(blocker.enabled) || !IsSceneVisibilityBlockerComponentValid(blocker)) return false;
        output.visibilityBlocker = blocker;
    }
    if ((componentBits & VisibilityCellBit) != 0U) {
        if (fileVersion < 18U) return false;
        VisibilityCellComponent cell{};
        std::uint32_t membership = 0U;
        std::uint32_t visibilityOverride = 0U;
        if (!input.ReadUInt32(cell.membershipMask) || !input.ReadUInt32(membership) || !input.ReadUInt32(visibilityOverride) || !input.ReadBool(cell.enabled) ||
            membership > static_cast<std::uint32_t>(VisibilityCellMembership::Exclude) || visibilityOverride > static_cast<std::uint32_t>(VisibilityCellOverride::ForceHidden)) return false;
        cell.membership = static_cast<VisibilityCellMembership>(membership);
        cell.visibilityOverride = static_cast<VisibilityCellOverride>(visibilityOverride);
        if (!IsVisibilityCellComponentValid(cell)) return false;
        output.visibilityCell = cell;
    }
    if ((componentBits & RegionPortalBit) != 0U) {
        if (fileVersion < 19U) return false;
        ScenePrefabRegionPortalComponent portal{};
        if (!input.ReadUInt64(portal.sourceCellNodeStableId) || !input.ReadUInt64(portal.targetCellNodeStableId) || !input.ReadUInt32(portal.purposes) || !input.ReadBool(portal.enabled) || !IsRegionPortalPurposeMaskValid(portal.purposes)) return false;
        output.regionPortal = portal;
    }
    if ((componentBits & AuxFrameBit) != 0U) {
        if (fileVersion < 20U) return false;
        AuxFrameComponent frame{};
        std::uint32_t mode = 0U;
        std::uint32_t width = 0U;
        std::uint32_t height = 0U;
        if (!input.ReadUInt32(mode) || !input.ReadUInt64(frame.imageTargetId) || !input.ReadUInt32(width) || !input.ReadUInt32(height) ||
            !SceneAssetPrimitiveCodec::ReadVec3(input, frame.mirrorPlaneNormal) || !input.ReadFloat(frame.mirrorPlaneOffset) || !input.ReadBool(frame.enabled) ||
            mode > static_cast<std::uint32_t>(AuxFrameMode::Panoramic) || width == 0U || width > UINT16_MAX || height == 0U || height > UINT16_MAX) return false;
        frame.mode = static_cast<AuxFrameMode>(mode);
        frame.width = static_cast<std::uint16_t>(width);
        frame.height = static_cast<std::uint16_t>(height);
        if (!IsAuxFrameComponentPersistable(frame)) return false;
        output.auxFrame = frame;
    }
    if ((componentBits & GeometrySwarmBit) != 0U) {
        if (fileVersion < 21U) return false;
        GeometrySwarmComponent swarm{};
        std::uint32_t columns = 0U, rows = 0U, layers = 0U;
        if (!input.ReadUInt64(swarm.meshAssetId) || !input.ReadUInt64(swarm.materialAssetId) || !input.ReadUInt32(swarm.instanceCount) ||
            !input.ReadUInt32(columns) || !input.ReadUInt32(rows) || !input.ReadUInt32(layers) || !SceneAssetPrimitiveCodec::ReadVec3(input, swarm.spacing) ||
            !input.ReadFloat(swarm.instanceScale) || !input.ReadUInt32(swarm.layer) || !input.ReadBool(swarm.castsShadow) ||
            !input.ReadBool(swarm.receivesShadow) || !input.ReadBool(swarm.enabled) || columns == 0U || columns > UINT16_MAX || rows == 0U || rows > UINT16_MAX || layers == 0U || layers > UINT16_MAX) return false;
        swarm.columns = static_cast<std::uint16_t>(columns); swarm.rows = static_cast<std::uint16_t>(rows); swarm.layers = static_cast<std::uint16_t>(layers);
        if (!IsGeometrySwarmComponentPersistable(swarm)) return false;
        output.geometrySwarm = swarm;
    }
    if ((componentBits & SurfaceCastBit) != 0U) {
        SurfaceCastComponent surfaceCast{};
        std::uint32_t content = 0U;
        if (!input.ReadUInt64(surfaceCast.materialAssetId) || !input.ReadUInt32(surfaceCast.receiverLayerMask) ||
            !input.ReadInt32(surfaceCast.order) || !input.ReadUInt32(content) || !input.ReadBool(surfaceCast.enabled) ||
            content > static_cast<std::uint32_t>(SurfaceCastContent::Detail)) return false;
        surfaceCast.content = static_cast<SurfaceCastContent>(content);
        if (!IsSurfaceCastComponentPersistable(surfaceCast)) return false;
        output.surfaceCast = surfaceCast;
    }
    if ((componentBits & FacingPanelBit) != 0U) {
        if (fileVersion < 23U) return false;
        FacingPanelComponent panel{};
        std::uint32_t mode = 0U;
        if (!input.ReadUInt32(mode) || !SceneAssetPrimitiveCodec::ReadVec3(input, panel.targetPoint) ||
            !SceneAssetPrimitiveCodec::ReadVec3(input, panel.axis) || !SceneAssetPrimitiveCodec::ReadVec3(input, panel.up) ||
            !input.ReadBool(panel.enabled) || mode > static_cast<std::uint32_t>(FacingPanelMode::Fixed)) return false;
        panel.mode = static_cast<FacingPanelMode>(mode);
        if (!IsFacingPanelComponentPersistable(panel)) return false;
        output.facingPanel = panel;
    }
    if ((componentBits & SpaceStrokeBit) != 0U) {
        if (fileVersion < 24U) return false;
        SpaceStrokeComponent stroke{};
        std::uint32_t mode = 0U;
        std::uint32_t splineSegments = 0U;
        if (!input.ReadUInt64(stroke.meshAssetId) || !input.ReadUInt64(stroke.materialAssetId) || !input.ReadUInt32(mode) ||
            !input.ReadFloat(stroke.width) || !input.ReadFloat(stroke.cableSag) || !input.ReadUInt32(splineSegments) ||
            !input.ReadUInt32(stroke.layer) || !input.ReadBool(stroke.castsShadow) || !input.ReadBool(stroke.receivesShadow) ||
            !input.ReadBool(stroke.enabled) || mode > static_cast<std::uint32_t>(SpaceStrokeMode::Cable) || splineSegments > UINT8_MAX) return false;
        stroke.mode = static_cast<SpaceStrokeMode>(mode);
        stroke.splineSegments = static_cast<std::uint8_t>(splineSegments);
        if (!IsSpaceStrokeComponentPersistable(stroke)) return false;
        output.spaceStroke = stroke;
    }
    if ((componentBits & HistoryRibbonBit) != 0U) {
        if (fileVersion < 25U) return false;
        HistoryRibbonComponent ribbon{};
        if (!input.ReadUInt64(ribbon.meshAssetId) || !input.ReadUInt64(ribbon.materialAssetId) ||
            !input.ReadFloat(ribbon.lifetimeSeconds) || !input.ReadFloat(ribbon.width) || !input.ReadFloat(ribbon.sampleIntervalSeconds) ||
            !input.ReadUInt32(ribbon.layer) || !input.ReadBool(ribbon.castsShadow) || !input.ReadBool(ribbon.receivesShadow) ||
            !input.ReadBool(ribbon.enabled) || !IsHistoryRibbonComponentPersistable(ribbon)) return false;
        output.historyRibbon = ribbon;
    }
    if ((componentBits & LensEchoBit) != 0U) {
        if (fileVersion < 26U) return false;
        ScenePrefabLensEchoComponent echo{};
        std::uint32_t occlusionRule = 0U;
        if (!input.ReadUInt64(echo.sourceNodeStableId) || !input.ReadUInt64(echo.profileMaterialAssetId) ||
            !input.ReadFloat(echo.intensity) || !input.ReadFloat(echo.size) || !input.ReadUInt32(echo.layer) ||
            !input.ReadUInt32(occlusionRule) || !input.ReadBool(echo.enabled) ||
            occlusionRule > static_cast<std::uint32_t>(LensEchoOcclusionRule::AlwaysVisible)) return false;
        echo.occlusionRule = static_cast<LensEchoOcclusionRule>(occlusionRule);
        LensEchoComponent validation{ .sourceEntityId = echo.sourceNodeStableId, .profileMaterialAssetId = echo.profileMaterialAssetId,
            .intensity = echo.intensity, .size = echo.size, .layer = echo.layer, .occlusionRule = echo.occlusionRule, .enabled = echo.enabled };
        if (!IsLensEchoComponentPersistable(validation)) return false;
        output.lensEcho = echo;
    }
    return true;
}

void SceneAssetComponentCodec::Write(std::vector<std::uint8_t>& output, const ScenePrefabNodeComponents& components) {
    SceneAssetBinaryIO::WriteUInt64(output, ComponentBits(components));
    if (components.camera.has_value()) {
        SceneAssetCameraComponentCodec::Write(output, *components.camera);
    }
    if (components.meshRenderer.has_value()) {
        SceneAssetRenderComponentCodec::WriteMeshRenderer(output, *components.meshRenderer);
    }
    if (components.light.has_value()) {
        SceneAssetRenderComponentCodec::WriteLight(output, *components.light);
    }
    if (components.input.has_value()) {
        SceneAssetInputComponentCodec::Write(output, *components.input);
    }
    if (components.rigidbody.has_value()) {
        SceneAssetPhysicsComponentCodec::WriteRigidbody(output, *components.rigidbody);
    }
    if (components.collider.has_value()) {
        SceneAssetPhysicsComponentCodec::WriteCollider(output, *components.collider);
    }
    if (components.characterController.has_value()) {
        SceneAssetPhysicsComponentCodec::WriteCharacterController(output, *components.characterController);
    }
    if (components.joint.has_value()) {
        SceneAssetPhysicsComponentCodec::WriteJoint(output, *components.joint);
    }
    if (components.tags.has_value()) {
        SceneAssetTagsComponentCodec::Write(output, *components.tags);
    }
    if (components.audioSource.has_value()) {
        SceneAssetAudioComponentCodec::WriteSource(output, *components.audioSource);
    }
    if (components.audioListener.has_value()) {
        SceneAssetAudioComponentCodec::WriteListener(output, *components.audioListener);
    }
    if (components.behaviour.has_value()) {
        SceneAssetBehaviourComponentCodec::Write(output, *components.behaviour);
    }
    if (components.animator.has_value()) {
        SceneAssetBinaryIO::WriteUInt64(output, components.animator->controllerAssetId);
        SceneAssetBinaryIO::WriteFloat(output, components.animator->speed);
        SceneAssetBinaryIO::WriteBool(output, components.animator->enabled);
        SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(components.animator->rootMotionOwner));
        SceneAssetBinaryIO::WriteFloat(output, components.animator->poseUpdateRateHz);
    }
    if (components.skeletonBinding.has_value()) {
        SceneAssetBinaryIO::WriteUInt64(output, components.skeletonBinding->skeletonAssetId);
        SceneAssetBinaryIO::WriteUInt64(output, components.skeletonBinding->skeletonCompatibilitySignature);
        SceneAssetBinaryIO::WriteBool(output, components.skeletonBinding->enabled);
    }
    if (components.deformedGeometry.has_value()) {
        const DrawD3DeformedGeometryComponent& geometry = *components.deformedGeometry;
        if (geometry.poseSource.IsValid()) {
            throw std::invalid_argument("Deformed Geometry scene serialization requires a stable pose-source node reference");
        }
        SceneAssetBinaryIO::WriteUInt64(output, geometry.skeletalMeshAssetId);
        SceneAssetBinaryIO::WriteUInt32(output, geometry.materialSlotOverrideCount);
        for (const std::uint64_t material : geometry.materialSlotAssetIds) SceneAssetBinaryIO::WriteUInt64(output, material);
        // Zero is the canonical component-owner pose source. Non-owner live ECS
        // handles are rejected above instead of being silently discarded.
        SceneAssetBinaryIO::WriteUInt64(output, 0U);
        SceneAssetBinaryIO::WriteInt32(output, geometry.lodBias);
        SceneAssetBinaryIO::WriteBool(output, geometry.lodEnabled);
        SceneAssetBinaryIO::WriteBool(output, geometry.fixedBounds);
        SceneAssetBinaryIO::WriteBool(output, geometry.castsShadow);
        SceneAssetBinaryIO::WriteBool(output, geometry.receivesShadow);
        SceneAssetBinaryIO::WriteUInt32(output, geometry.layer);
        SceneAssetBinaryIO::WriteBool(output, geometry.enabled);
    }
    if (components.uiDocument.has_value()) {
        SceneAssetBinaryIO::WriteUInt64(output, components.uiDocument->documentAssetId);
        SceneAssetBinaryIO::WriteBool(output, components.uiDocument->enabled);
    }
    if (components.navAgent.has_value()) {
        const NavAgent& agent = *components.navAgent;
        SceneAssetBinaryIO::WriteFloat(output, agent.radius);
        SceneAssetBinaryIO::WriteFloat(output, agent.height);
        SceneAssetBinaryIO::WriteFloat(output, agent.maxSpeed);
        SceneAssetBinaryIO::WriteFloat(output, agent.acceleration);
        SceneAssetBinaryIO::WriteFloat(output, agent.angularSpeedDegrees);
        SceneAssetBinaryIO::WriteFloat(output, agent.stoppingDistance);
        SceneAssetBinaryIO::WriteUInt32(output, agent.areaMask);
        SceneAssetPrimitiveCodec::WriteVec3(output, agent.destination);
        SceneAssetPrimitiveCodec::WriteVec3(output, agent.velocity);
        SceneAssetBinaryIO::WriteFloat(output, agent.remainingDistance);
        SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(agent.pathStatus));
        SceneAssetBinaryIO::WriteBool(output, agent.enabled);
    }
    if (components.navObstacle.has_value()) {
        const NavObstacle& obstacle = *components.navObstacle;
        SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(obstacle.shape));
        SceneAssetPrimitiveCodec::WriteVec3(output, obstacle.center);
        SceneAssetPrimitiveCodec::WriteVec3(output, obstacle.size);
        SceneAssetBinaryIO::WriteFloat(output, obstacle.radius);
        SceneAssetBinaryIO::WriteFloat(output, obstacle.height);
        SceneAssetBinaryIO::WriteUInt32(output, obstacle.area);
        SceneAssetBinaryIO::WriteBool(output, obstacle.carve);
        SceneAssetBinaryIO::WriteBool(output, obstacle.enabled);
    }
    if (components.regionShape.has_value()) {
        const RegionShapeComponent& regionShape = *components.regionShape;
        SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(regionShape.kind));
        SceneAssetPrimitiveCodec::WriteVec3(output, regionShape.center);
        SceneAssetPrimitiveCodec::WriteVec3(output, regionShape.size);
        SceneAssetBinaryIO::WriteFloat(output, regionShape.radius);
        SceneAssetBinaryIO::WriteFloat(output, regionShape.height);
        SceneAssetBinaryIO::WriteBool(output, regionShape.enabled);
    }
    if (components.guideCurve.has_value()) {
        const GuideCurveComponent& guideCurve = *components.guideCurve;
        SceneAssetBinaryIO::WriteUInt32(output, guideCurve.controlPointCount);
        SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(guideCurve.interpolation));
        SceneAssetBinaryIO::WriteBool(output, guideCurve.closed);
        SceneAssetBinaryIO::WriteBool(output, guideCurve.enabled);
        for (std::uint32_t index = 0U; index < guideCurve.controlPointCount; ++index) SceneAssetPrimitiveCodec::WriteVec3(output, guideCurve.controlPoints[index]);
    }
    if (components.contentInstance.has_value()) {
        const ContentInstanceComponent& content = *components.contentInstance;
        SceneAssetBinaryIO::WriteUInt64(output, content.assetId);
        SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(content.kind));
        SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(content.lifetime));
        SceneAssetBinaryIO::WriteBool(output, content.active);
    }
    if (components.streamFocus.has_value()) {
        const StreamFocusComponent& focus = *components.streamFocus;
        SceneAssetBinaryIO::WriteFloat(output, focus.innerRadius);
        SceneAssetBinaryIO::WriteFloat(output, focus.outerRadius);
        SceneAssetBinaryIO::WriteInt32(output, focus.priority);
        SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(focus.loadMask));
        SceneAssetBinaryIO::WriteBool(output, focus.enabled);
    }
    if (components.worldBackdrop.has_value()) {
        const WorldBackdropComponent& backdrop = *components.worldBackdrop;
        SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(backdrop.mode));
        SceneAssetPrimitiveCodec::WriteVec3(output, backdrop.color);
        SceneAssetPrimitiveCodec::WriteVec3(output, backdrop.horizonColor);
        SceneAssetPrimitiveCodec::WriteVec3(output, backdrop.zenithColor);
        SceneAssetBinaryIO::WriteUInt64(output, backdrop.environmentAssetId);
        SceneAssetBinaryIO::WriteFloat(output, backdrop.horizonHeight);
        SceneAssetBinaryIO::WriteFloat(output, backdrop.gradientExponent);
        SceneAssetBinaryIO::WriteInt32(output, backdrop.priority);
        SceneAssetBinaryIO::WriteBool(output, backdrop.enabled);
    }
    if (components.ambientRadiance.has_value()) {
        const AmbientRadianceComponent& ambient = *components.ambientRadiance;
        SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(ambient.mode));
        SceneAssetPrimitiveCodec::WriteVec3(output, ambient.color);
        SceneAssetPrimitiveCodec::WriteVec3(output, ambient.horizonColor);
        SceneAssetPrimitiveCodec::WriteVec3(output, ambient.zenithColor);
        SceneAssetBinaryIO::WriteUInt64(output, ambient.environmentAssetId);
        SceneAssetBinaryIO::WriteFloat(output, ambient.intensity);
        SceneAssetBinaryIO::WriteFloat(output, ambient.diffuseIntensity);
        SceneAssetBinaryIO::WriteFloat(output, ambient.specularIntensity);
        SceneAssetBinaryIO::WriteInt32(output, ambient.priority);
        SceneAssetBinaryIO::WriteBool(output, ambient.enabled);
    }
    if (components.detailSwitch.has_value()) {
        const SceneDetailSwitchComponent& detail = *components.detailSwitch;
        SceneAssetBinaryIO::WriteUInt64(output, detail.groupId);
        SceneAssetBinaryIO::WriteUInt32(output, detail.minimumLod);
        SceneAssetBinaryIO::WriteUInt32(output, detail.maximumLod);
        SceneAssetBinaryIO::WriteFloat(output, detail.promoteCoverage);
        SceneAssetBinaryIO::WriteFloat(output, detail.demoteCoverage);
        SceneAssetBinaryIO::WriteBool(output, detail.enabled);
    }
    if (components.visibilityBlocker.has_value()) {
        const SceneVisibilityBlockerComponent& blocker = *components.visibilityBlocker;
        SceneAssetPrimitiveCodec::WriteVec3(output, blocker.localCenter);
        SceneAssetPrimitiveCodec::WriteVec3(output, blocker.size);
        SceneAssetBinaryIO::WriteBool(output, blocker.enabled);
    }
    if (components.visibilityCell.has_value()) {
        const VisibilityCellComponent& cell = *components.visibilityCell;
        SceneAssetBinaryIO::WriteUInt32(output, cell.membershipMask);
        SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(cell.membership));
        SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(cell.visibilityOverride));
        SceneAssetBinaryIO::WriteBool(output, cell.enabled);
    }
    if (components.regionPortal.has_value()) {
        const ScenePrefabRegionPortalComponent& portal = *components.regionPortal;
        SceneAssetBinaryIO::WriteUInt64(output, portal.sourceCellNodeStableId);
        SceneAssetBinaryIO::WriteUInt64(output, portal.targetCellNodeStableId);
        SceneAssetBinaryIO::WriteUInt32(output, portal.purposes);
        SceneAssetBinaryIO::WriteBool(output, portal.enabled);
    }
    if (components.auxFrame.has_value()) {
        const AuxFrameComponent& frame = *components.auxFrame;
        SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(frame.mode));
        SceneAssetBinaryIO::WriteUInt64(output, frame.imageTargetId);
        SceneAssetBinaryIO::WriteUInt32(output, frame.width);
        SceneAssetBinaryIO::WriteUInt32(output, frame.height);
        SceneAssetPrimitiveCodec::WriteVec3(output, frame.mirrorPlaneNormal);
        SceneAssetBinaryIO::WriteFloat(output, frame.mirrorPlaneOffset);
        SceneAssetBinaryIO::WriteBool(output, frame.enabled);
    }
    if (components.geometrySwarm.has_value()) {
        const GeometrySwarmComponent& swarm = *components.geometrySwarm;
        SceneAssetBinaryIO::WriteUInt64(output, swarm.meshAssetId);
        SceneAssetBinaryIO::WriteUInt64(output, swarm.materialAssetId);
        SceneAssetBinaryIO::WriteUInt32(output, swarm.instanceCount);
        SceneAssetBinaryIO::WriteUInt32(output, swarm.columns);
        SceneAssetBinaryIO::WriteUInt32(output, swarm.rows);
        SceneAssetBinaryIO::WriteUInt32(output, swarm.layers);
        SceneAssetPrimitiveCodec::WriteVec3(output, swarm.spacing);
        SceneAssetBinaryIO::WriteFloat(output, swarm.instanceScale);
        SceneAssetBinaryIO::WriteUInt32(output, swarm.layer);
        SceneAssetBinaryIO::WriteBool(output, swarm.castsShadow);
        SceneAssetBinaryIO::WriteBool(output, swarm.receivesShadow);
        SceneAssetBinaryIO::WriteBool(output, swarm.enabled);
    }
    if (components.surfaceCast.has_value()) {
        const SurfaceCastComponent& surfaceCast = *components.surfaceCast;
        SceneAssetBinaryIO::WriteUInt64(output, surfaceCast.materialAssetId);
        SceneAssetBinaryIO::WriteUInt32(output, surfaceCast.receiverLayerMask);
        SceneAssetBinaryIO::WriteInt32(output, surfaceCast.order);
        SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(surfaceCast.content));
        SceneAssetBinaryIO::WriteBool(output, surfaceCast.enabled);
    }
    if (components.facingPanel.has_value()) {
        const FacingPanelComponent& panel = *components.facingPanel;
        SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(panel.mode));
        SceneAssetPrimitiveCodec::WriteVec3(output, panel.targetPoint);
        SceneAssetPrimitiveCodec::WriteVec3(output, panel.axis);
        SceneAssetPrimitiveCodec::WriteVec3(output, panel.up);
        SceneAssetBinaryIO::WriteBool(output, panel.enabled);
    }
    if (components.spaceStroke.has_value()) {
        const SpaceStrokeComponent& stroke = *components.spaceStroke;
        SceneAssetBinaryIO::WriteUInt64(output, stroke.meshAssetId);
        SceneAssetBinaryIO::WriteUInt64(output, stroke.materialAssetId);
        SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(stroke.mode));
        SceneAssetBinaryIO::WriteFloat(output, stroke.width);
        SceneAssetBinaryIO::WriteFloat(output, stroke.cableSag);
        SceneAssetBinaryIO::WriteUInt32(output, stroke.splineSegments);
        SceneAssetBinaryIO::WriteUInt32(output, stroke.layer);
        SceneAssetBinaryIO::WriteBool(output, stroke.castsShadow);
        SceneAssetBinaryIO::WriteBool(output, stroke.receivesShadow);
        SceneAssetBinaryIO::WriteBool(output, stroke.enabled);
    }
    if (components.historyRibbon.has_value()) {
        const HistoryRibbonComponent& ribbon = *components.historyRibbon;
        SceneAssetBinaryIO::WriteUInt64(output, ribbon.meshAssetId);
        SceneAssetBinaryIO::WriteUInt64(output, ribbon.materialAssetId);
        SceneAssetBinaryIO::WriteFloat(output, ribbon.lifetimeSeconds);
        SceneAssetBinaryIO::WriteFloat(output, ribbon.width);
        SceneAssetBinaryIO::WriteFloat(output, ribbon.sampleIntervalSeconds);
        SceneAssetBinaryIO::WriteUInt32(output, ribbon.layer);
        SceneAssetBinaryIO::WriteBool(output, ribbon.castsShadow);
        SceneAssetBinaryIO::WriteBool(output, ribbon.receivesShadow);
        SceneAssetBinaryIO::WriteBool(output, ribbon.enabled);
    }
    if (components.lensEcho.has_value()) {
        const ScenePrefabLensEchoComponent& echo = *components.lensEcho;
        SceneAssetBinaryIO::WriteUInt64(output, echo.sourceNodeStableId);
        SceneAssetBinaryIO::WriteUInt64(output, echo.profileMaterialAssetId);
        SceneAssetBinaryIO::WriteFloat(output, echo.intensity);
        SceneAssetBinaryIO::WriteFloat(output, echo.size);
        SceneAssetBinaryIO::WriteUInt32(output, echo.layer);
        SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(echo.occlusionRule));
        SceneAssetBinaryIO::WriteBool(output, echo.enabled);
    }
}

} // namespace kb::scene
