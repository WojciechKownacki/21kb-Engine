#include "scene/asset/io/components/SceneAssetPhysicsComponentCodec.hpp"

#include "scene/asset/io/SceneAssetPrimitiveCodec.hpp"

namespace kb::scene {

bool SceneAssetPhysicsComponentCodec::ReadRigidbody(SceneAssetBinaryIO::ByteReader& input, RigidbodyComponent& output) {
    std::uint32_t bodyType = 0;
    bool useGravity = true;
    bool lockRotation = false;
    bool useContinuousCollision = false;
    if (!input.ReadUInt32(bodyType) ||
        bodyType > static_cast<std::uint32_t>(RigidbodyBodyType::Kinematic) ||
        !input.ReadFloat(output.mass) ||
        !SceneAssetPrimitiveCodec::ReadVec3(input, output.linearVelocity) ||
        !SceneAssetPrimitiveCodec::ReadVec3(input, output.angularVelocity) ||
        !input.ReadFloat(output.gravityScale) ||
        !input.ReadBool(useGravity) ||
        !input.ReadBool(lockRotation) ||
        !input.ReadBool(useContinuousCollision)) {
        return false;
    }
    output.bodyType = static_cast<RigidbodyBodyType>(bodyType);
    output.useGravity = useGravity;
    output.lockRotation = lockRotation;
    output.useContinuousCollision = useContinuousCollision;
    return true;
}

void SceneAssetPhysicsComponentCodec::WriteRigidbody(std::vector<std::uint8_t>& output, const RigidbodyComponent& rigidbody) {
    SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(rigidbody.bodyType));
    SceneAssetBinaryIO::WriteFloat(output, rigidbody.mass);
    SceneAssetPrimitiveCodec::WriteVec3(output, rigidbody.linearVelocity);
    SceneAssetPrimitiveCodec::WriteVec3(output, rigidbody.angularVelocity);
    SceneAssetBinaryIO::WriteFloat(output, rigidbody.gravityScale);
    SceneAssetBinaryIO::WriteUInt8(output, rigidbody.useGravity ? 1U : 0U);
    SceneAssetBinaryIO::WriteUInt8(output, rigidbody.lockRotation ? 1U : 0U);
    SceneAssetBinaryIO::WriteUInt8(output, rigidbody.useContinuousCollision ? 1U : 0U);
}

bool SceneAssetPhysicsComponentCodec::ReadCollider(SceneAssetBinaryIO::ByteReader& input, ColliderComponent& output) {
    std::uint32_t shape = 0;
    bool trigger = false;
    if (!input.ReadUInt32(shape) ||
        shape > static_cast<std::uint32_t>(ColliderShape::Capsule) ||
        !SceneAssetPrimitiveCodec::ReadVec3(input, output.center) ||
        !SceneAssetPrimitiveCodec::ReadVec3(input, output.boxSize) ||
        !input.ReadFloat(output.radius) ||
        !input.ReadFloat(output.height) ||
        !input.ReadBool(trigger) ||
        !input.ReadFloat(output.friction) ||
        !input.ReadFloat(output.restitution) ||
        !input.ReadUInt32(output.layer)) {
        return false;
    }
    output.shape = static_cast<ColliderShape>(shape);
    output.trigger = trigger;
    return true;
}

void SceneAssetPhysicsComponentCodec::WriteCollider(std::vector<std::uint8_t>& output, const ColliderComponent& collider) {
    SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(collider.shape));
    SceneAssetPrimitiveCodec::WriteVec3(output, collider.center);
    SceneAssetPrimitiveCodec::WriteVec3(output, collider.boxSize);
    SceneAssetBinaryIO::WriteFloat(output, collider.radius);
    SceneAssetBinaryIO::WriteFloat(output, collider.height);
    SceneAssetBinaryIO::WriteUInt8(output, collider.trigger ? 1U : 0U);
    SceneAssetBinaryIO::WriteFloat(output, collider.friction);
    SceneAssetBinaryIO::WriteFloat(output, collider.restitution);
    SceneAssetBinaryIO::WriteUInt32(output, collider.layer);
}

bool SceneAssetPhysicsComponentCodec::ReadCharacterController(SceneAssetBinaryIO::ByteReader& input, CharacterControllerComponent& output) {
    return SceneAssetPrimitiveCodec::ReadVec3(input, output.center) &&
        input.ReadFloat(output.radius) &&
        input.ReadFloat(output.height) &&
        input.ReadFloat(output.slopeLimitDegrees) &&
        input.ReadFloat(output.stepOffset) &&
        input.ReadFloat(output.gravityScale) &&
        input.ReadBool(output.useGravity);
}

void SceneAssetPhysicsComponentCodec::WriteCharacterController(std::vector<std::uint8_t>& output, const CharacterControllerComponent& characterController) {
    SceneAssetPrimitiveCodec::WriteVec3(output, characterController.center);
    SceneAssetBinaryIO::WriteFloat(output, characterController.radius);
    SceneAssetBinaryIO::WriteFloat(output, characterController.height);
    SceneAssetBinaryIO::WriteFloat(output, characterController.slopeLimitDegrees);
    SceneAssetBinaryIO::WriteFloat(output, characterController.stepOffset);
    SceneAssetBinaryIO::WriteFloat(output, characterController.gravityScale);
    SceneAssetBinaryIO::WriteUInt8(output, characterController.useGravity ? 1U : 0U);
}

bool SceneAssetPhysicsComponentCodec::ReadJoint(SceneAssetBinaryIO::ByteReader& input, ScenePrefabJointComponent& output) {
    std::uint32_t type = 0;
    if (!input.ReadUInt32(type) ||
        type > static_cast<std::uint32_t>(JointType::Point) ||
        !input.ReadUInt64(output.connectedNodeStableId) ||
        !SceneAssetPrimitiveCodec::ReadVec3(input, output.anchor) ||
        !SceneAssetPrimitiveCodec::ReadVec3(input, output.connectedAnchor) ||
        !SceneAssetPrimitiveCodec::ReadVec3(input, output.axis) ||
        !input.ReadFloat(output.minLimit) ||
        !input.ReadFloat(output.maxLimit) ||
        !input.ReadBool(output.enableLimit)) {
        return false;
    }
    output.type = static_cast<JointType>(type);
    return true;
}

void SceneAssetPhysicsComponentCodec::WriteJoint(std::vector<std::uint8_t>& output, const ScenePrefabJointComponent& joint) {
    SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(joint.type));
    SceneAssetBinaryIO::WriteUInt64(output, joint.connectedNodeStableId);
    SceneAssetPrimitiveCodec::WriteVec3(output, joint.anchor);
    SceneAssetPrimitiveCodec::WriteVec3(output, joint.connectedAnchor);
    SceneAssetPrimitiveCodec::WriteVec3(output, joint.axis);
    SceneAssetBinaryIO::WriteFloat(output, joint.minLimit);
    SceneAssetBinaryIO::WriteFloat(output, joint.maxLimit);
    SceneAssetBinaryIO::WriteUInt8(output, joint.enableLimit ? 1U : 0U);
}

} // namespace kb::scene
