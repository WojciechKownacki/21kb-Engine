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

namespace kb::scene {
namespace {

enum SceneNodeComponentBits : std::uint32_t {
    CameraBit = 1U << 0U,
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
};

constexpr std::uint32_t KnownComponentBits = CameraBit |
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
      ContentInstanceBit | StreamFocusBit | WorldBackdropBit;

[[nodiscard]] std::uint32_t ComponentBits(const ScenePrefabNodeComponents& components) noexcept {
    std::uint32_t componentBits = 0;
    componentBits |= components.camera.has_value() ? CameraBit : 0U;
    componentBits |= components.meshRenderer.has_value() ? MeshRendererBit : 0U;
    componentBits |= components.light.has_value() ? LightBit : 0U;
    componentBits |= components.input.has_value() ? InputBit : 0U;
    componentBits |= components.rigidbody.has_value() ? RigidbodyBit : 0U;
    componentBits |= components.collider.has_value() ? ColliderBit : 0U;
    componentBits |= components.audioSource.has_value() ? AudioSourceBit : 0U;
    componentBits |= components.audioListener.has_value() ? AudioListenerBit : 0U;
    componentBits |= components.behaviour.has_value() ? BehaviourBit : 0U;
    componentBits |= components.tags.has_value() ? TagsBit : 0U;
    componentBits |= components.characterController.has_value() ? CharacterControllerBit : 0U;
    componentBits |= components.joint.has_value() ? JointBit : 0U;
    componentBits |= components.animator.has_value() ? AnimatorBit : 0U;
    componentBits |= components.uiDocument.has_value() ? UIDocumentBit : 0U;
    componentBits |= components.navAgent.has_value() ? NavAgentBit : 0U;
    componentBits |= components.navObstacle.has_value() ? NavObstacleBit : 0U;
    componentBits |= components.regionShape.has_value() ? RegionShapeBit : 0U;
    componentBits |= components.guideCurve.has_value() ? GuideCurveBit : 0U;
      componentBits |= components.contentInstance.has_value() ? ContentInstanceBit : 0U;
      componentBits |= components.streamFocus.has_value() ? StreamFocusBit : 0U;
      componentBits |= components.worldBackdrop.has_value() ? WorldBackdropBit : 0U;
    return componentBits;
}

} // namespace

bool SceneAssetComponentCodec::Read(SceneAssetBinaryIO::ByteReader& input, std::uint32_t fileVersion, ScenePrefabNodeComponents& output) {
    std::uint32_t componentBits = 0;
    if (!input.ReadUInt32(componentBits) || (componentBits & ~KnownComponentBits) != 0U) {
        return false;
    }

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
        output.animator = animator;
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
    return true;
}

void SceneAssetComponentCodec::Write(std::vector<std::uint8_t>& output, const ScenePrefabNodeComponents& components) {
    SceneAssetBinaryIO::WriteUInt32(output, ComponentBits(components));
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
}

} // namespace kb::scene
