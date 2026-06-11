#include "scene/asset/io/SceneAssetReader.hpp"

#include "engine/scene/SceneAssetMeta.hpp"
#include "scene/asset/io/SceneAssetBinaryIO.hpp"
#include "scene/asset/io/SceneAssetFormat.hpp"
#include "scene/asset/io/SceneAssetIntegrity.hpp"
#include "scene/asset/io/SceneAssetMetaReader.hpp"
#include "scene/prefab/ScenePrefabValidator.hpp"

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace kb::scene {
namespace {

enum SceneNodeComponentBits : std::uint32_t {
    CameraBit = 1U << 0U,
    MeshRendererBit = 1U << 1U,
    LightBit = 1U << 2U,
    InputBit = 1U << 3U,
    RigidbodyBit = 1U << 4U,
    ColliderBit = 1U << 5U,
};

using SceneAssetBinaryIO::ByteReader;
using SceneAssetBinaryIO::ReadAllBytes;

[[nodiscard]] std::filesystem::path MetaPathFor(const std::filesystem::path& scenePath) {
    std::filesystem::path metaPath = scenePath;
    metaPath.replace_extension(".meta");
    return metaPath;
}

[[nodiscard]] std::uint32_t RootCount(const ScenePrefab& prefab) noexcept {
    std::uint32_t count = 0;
    for (const ScenePrefabNodeDesc& node : prefab.Nodes()) {
        if (node.parentNode == ScenePrefabNodeDesc::NoParent) {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] bool ReadVec3(ByteReader& input, Vec3& output) {
    return input.ReadFloat(output.x) && input.ReadFloat(output.y) && input.ReadFloat(output.z);
}

[[nodiscard]] bool ReadQuat(ByteReader& input, Quat& output) {
    return input.ReadFloat(output.x) && input.ReadFloat(output.y) && input.ReadFloat(output.z) && input.ReadFloat(output.w);
}

[[nodiscard]] bool ReadCamera(ByteReader& input, CameraComponent& output) {
    std::uint32_t projection = 0;
    bool primary = false;
    if (!input.ReadUInt32(projection) ||
        projection > static_cast<std::uint32_t>(CameraProjection::Orthographic) ||
        !input.ReadFloat(output.verticalFovDegrees) ||
        !input.ReadFloat(output.orthographicHeight) ||
        !input.ReadFloat(output.nearClip) ||
        !input.ReadFloat(output.farClip) ||
        !input.ReadBool(primary)) {
        return false;
    }
    output.projection = static_cast<CameraProjection>(projection);
    output.primary = primary;
    return true;
}

[[nodiscard]] bool ReadMeshRenderer(ByteReader& input, MeshRendererComponent& output) {
    if (!input.ReadUInt64(output.meshAssetId) ||
        !input.ReadUInt64(output.materialAssetId) ||
        !input.ReadUInt32(output.materialSlotOverrideCount) ||
        output.materialSlotOverrideCount > kMaxMeshRendererMaterialSlotOverrides) {
        return false;
    }
    for (std::uint64_t& materialSlotAssetId : output.materialSlotAssetIds) {
        if (!input.ReadUInt64(materialSlotAssetId)) {
            return false;
        }
    }
    return input.ReadBool(output.castsShadow) && input.ReadBool(output.receivesShadow);
}

[[nodiscard]] bool ReadLight(ByteReader& input, LightComponent& output) {
    std::uint32_t kind = 0;
    bool castsShadow = true;
    if (!input.ReadUInt32(kind) ||
        kind > static_cast<std::uint32_t>(LightKind::Tube) ||
        !ReadVec3(input, output.color) ||
        !input.ReadFloat(output.intensity) ||
        !input.ReadFloat(output.range) ||
        !input.ReadFloat(output.innerConeDegrees) ||
        !input.ReadFloat(output.outerConeDegrees) ||
        !input.ReadFloat(output.areaWidth) ||
        !input.ReadFloat(output.areaHeight) ||
        !input.ReadFloat(output.contactShadowLength) ||
        !input.ReadFloat(output.volumetricScattering) ||
        !input.ReadBool(castsShadow)) {
        return false;
    }
    output.kind = static_cast<LightKind>(kind);
    output.castsShadow = castsShadow;
    return true;
}

[[nodiscard]] bool ReadInput(ByteReader& input, InputComponent& output) {
    std::uint32_t priority = 0;
    if (!input.ReadUInt64(output.mappingContextAssetId) ||
        !input.ReadUInt32(priority) ||
        !input.ReadBool(output.enabled)) {
        return false;
    }
    output.priority = static_cast<std::int32_t>(priority);
    return true;
}

[[nodiscard]] bool ReadRigidbody(ByteReader& input, RigidbodyComponent& output) {
    std::uint32_t bodyType = 0;
    bool useGravity = true;
    bool lockRotation = false;
    if (!input.ReadUInt32(bodyType) ||
        bodyType > static_cast<std::uint32_t>(RigidbodyBodyType::Kinematic) ||
        !input.ReadFloat(output.mass) ||
        !ReadVec3(input, output.linearVelocity) ||
        !ReadVec3(input, output.angularVelocity) ||
        !input.ReadFloat(output.gravityScale) ||
        !input.ReadBool(useGravity) ||
        !input.ReadBool(lockRotation)) {
        return false;
    }
    output.bodyType = static_cast<RigidbodyBodyType>(bodyType);
    output.useGravity = useGravity;
    output.lockRotation = lockRotation;
    return true;
}

[[nodiscard]] bool ReadCollider(ByteReader& input, ColliderComponent& output) {
    std::uint32_t shape = 0;
    bool trigger = false;
    if (!input.ReadUInt32(shape) ||
        shape > static_cast<std::uint32_t>(ColliderShape::Capsule) ||
        !ReadVec3(input, output.center) ||
        !ReadVec3(input, output.boxSize) ||
        !input.ReadFloat(output.radius) ||
        !input.ReadFloat(output.height) ||
        !input.ReadBool(trigger)) {
        return false;
    }
    output.shape = static_cast<ColliderShape>(shape);
    output.trigger = trigger;
    return true;
}

[[nodiscard]] bool ReadNestedOverride(ByteReader& input, ScenePrefabPropertyOverride& output) {
    std::uint32_t flag = 0;
    if (!input.ReadUInt32(output.nodeIndex) ||
        !input.ReadUInt32(flag) ||
        !input.ReadString(output.propertyPath) ||
        !input.ReadString(output.value)) {
        return false;
    }
    output.flag = static_cast<ScenePrefabOverrideFlag>(flag);
    return true;
}

[[nodiscard]] bool ReadNode(ByteReader& input, ScenePrefabNodeDesc& output) {
    std::uint32_t nestedOverrideCount = 0;
    if (!input.ReadString(output.name) ||
        !input.ReadString(output.nestedPrefabGuid) ||
        !input.ReadUInt32(nestedOverrideCount) ||
        nestedOverrideCount > SceneAssetFormat::MaxNestedOverrideCount) {
        return false;
    }
    output.nestedPrefabOverrides.clear();
    output.nestedPrefabOverrides.reserve(nestedOverrideCount);
    for (std::uint32_t index = 0U; index < nestedOverrideCount; ++index) {
        ScenePrefabPropertyOverride property;
        if (!ReadNestedOverride(input, property)) {
            return false;
        }
        output.nestedPrefabOverrides.push_back(std::move(property));
    }

    std::uint32_t componentBits = 0;
    if (!input.ReadUInt32(output.parentNode) ||
        !ReadVec3(input, output.transform.localPosition) ||
        !ReadQuat(input, output.transform.localRotation) ||
        !ReadVec3(input, output.transform.localScale) ||
        !input.ReadBool(output.visibility.visible) ||
        !input.ReadUInt32(componentBits) ||
        (componentBits & ~(CameraBit | MeshRendererBit | LightBit | InputBit | RigidbodyBit | ColliderBit)) != 0U) {
        return false;
    }

    if ((componentBits & CameraBit) != 0U) {
        CameraComponent camera;
        if (!ReadCamera(input, camera)) {
            return false;
        }
        output.components.camera = camera;
    }
    if ((componentBits & MeshRendererBit) != 0U) {
        MeshRendererComponent meshRenderer;
        if (!ReadMeshRenderer(input, meshRenderer)) {
            return false;
        }
        output.components.meshRenderer = meshRenderer;
    }
    if ((componentBits & LightBit) != 0U) {
        LightComponent light;
        if (!ReadLight(input, light)) {
            return false;
        }
        output.components.light = light;
    }
    if ((componentBits & InputBit) != 0U) {
        InputComponent inputComponent;
        if (!ReadInput(input, inputComponent)) {
            return false;
        }
        output.components.input = inputComponent;
    }
    if ((componentBits & RigidbodyBit) != 0U) {
        RigidbodyComponent rigidbody;
        if (!ReadRigidbody(input, rigidbody)) {
            return false;
        }
        output.components.rigidbody = rigidbody;
    }
    if ((componentBits & ColliderBit) != 0U) {
        ColliderComponent collider;
        if (!ReadCollider(input, collider)) {
            return false;
        }
        output.components.collider = collider;
    }
    return true;
}

[[nodiscard]] SceneDocumentLoadResult ValidateMeta(const std::filesystem::path& path, const SceneDocument& scene, std::uint32_t rootCount, std::uint32_t nodeCount) {
    const SceneAssetMetaReadResult metaResult = SceneAssetMetaReader::Read(MetaPathFor(path));
    if (!metaResult.succeeded) {
        return SceneDocumentLoadResult{ .succeeded = false, .document = {}, .error = metaResult.error };
    }

    const SceneAssetIntegrity integrity = SceneAssetIntegrityService::ComputeFile(path);
    const SceneAssetMeta& meta = metaResult.meta;
    if (meta.contentHashFnv1a64 != integrity.contentHashFnv1a64 ||
        meta.contentChecksumCrc32 != integrity.contentChecksumCrc32 ||
        meta.byteSize != integrity.byteSize) {
        return SceneDocumentLoadResult{ .succeeded = false, .document = {}, .error = "Scene asset integrity does not match its .meta file." };
    }
    if (meta.sceneGuid != scene.guid || meta.sceneName != scene.name || meta.worldType != scene.worldType ||
        meta.rootCount != rootCount || meta.nodeCount != nodeCount) {
        return SceneDocumentLoadResult{ .succeeded = false, .document = {}, .error = "Scene asset content summary does not match its .meta file." };
    }
    return SceneDocumentLoadResult{ .succeeded = true, .document = scene, .error = {} };
}

} // namespace

SceneDocumentLoadResult SceneAssetReader::Read(const std::filesystem::path& path) {
    std::vector<std::uint8_t> bytes = ReadAllBytes(path);
    if (bytes.empty()) {
        return SceneDocumentLoadResult{ .succeeded = false, .document = {}, .error = "Scene asset could not be opened." };
    }

    ByteReader input{ std::move(bytes) };
    std::array<std::uint8_t, SceneAssetFormat::Magic.size()> magic{};
    std::uint32_t fileVersion = 0;
    if (!input.ReadRaw(magic.data(), magic.size()) ||
        magic != SceneAssetFormat::Magic ||
        !input.ReadUInt32(fileVersion) ||
        fileVersion == 0U ||
        fileVersion > SceneDocument::CurrentFileVersion) {
        return SceneDocumentLoadResult{ .succeeded = false, .document = {}, .error = "Scene asset header is invalid." };
    }

    SceneDocument scene;
    std::uint32_t rootCount = 0;
    std::uint32_t nodeCount = 0;
    if (!input.ReadString(scene.guid) ||
        !input.ReadString(scene.name) ||
        !input.ReadString(scene.worldType) ||
        !input.ReadUInt32(rootCount) ||
        !input.ReadUInt32(nodeCount) ||
        nodeCount > SceneAssetFormat::MaxNodeCount) {
        return SceneDocumentLoadResult{ .succeeded = false, .document = {}, .error = "Scene asset descriptor fields are invalid." };
    }
    scene.fileVersion = fileVersion;
    scene.worldPrefab.Reserve(nodeCount);
    for (std::uint32_t nodeIndex = 0U; nodeIndex < nodeCount; ++nodeIndex) {
        ScenePrefabNodeDesc node;
        if (!ReadNode(input, node)) {
            return SceneDocumentLoadResult{ .succeeded = false, .document = {}, .error = "Scene asset node list is invalid." };
        }
        static_cast<void>(scene.worldPrefab.AddNode(std::move(node)));
    }

    if (!input.Exhausted()) {
        return SceneDocumentLoadResult{ .succeeded = false, .document = {}, .error = "Scene asset contains trailing data." };
    }
    if (!ScenePrefabValidator::IsValid(scene.worldPrefab) || RootCount(scene.worldPrefab) != rootCount) {
        return SceneDocumentLoadResult{ .succeeded = false, .document = {}, .error = "Scene asset hierarchy is invalid." };
    }

    SceneDocumentLoadResult metaValidation = ValidateMeta(path, scene, rootCount, nodeCount);
    if (!metaValidation.succeeded) {
        return metaValidation;
    }
    return SceneDocumentLoadResult{ .succeeded = true, .document = std::move(scene), .error = {} };
}

} // namespace kb::scene
