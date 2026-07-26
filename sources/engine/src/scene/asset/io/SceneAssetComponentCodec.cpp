#include "scene/asset/io/SceneAssetComponentCodec.hpp"

#include "scene/asset/io/components/SceneAssetAudioComponentCodec.hpp"
#include "scene/asset/io/components/SceneAssetBehaviourComponentCodec.hpp"
#include "scene/asset/io/components/SceneAssetCameraComponentCodec.hpp"
#include "scene/asset/io/components/SceneAssetInputComponentCodec.hpp"
#include "scene/asset/io/components/SceneAssetPhysicsComponentCodec.hpp"
#include "scene/asset/io/components/SceneAssetRenderComponentCodec.hpp"
#include "scene/asset/io/components/SceneAssetTagsComponentCodec.hpp"

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
    JointBit;

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
}

} // namespace kb::scene
