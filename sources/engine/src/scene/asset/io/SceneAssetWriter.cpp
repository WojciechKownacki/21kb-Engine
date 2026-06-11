#include "scene/asset/io/SceneAssetWriter.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/scene/SceneAssetMeta.hpp"
#include "scene/asset/io/SceneAssetBinaryIO.hpp"
#include "scene/asset/io/SceneAssetFormat.hpp"
#include "scene/asset/io/SceneAssetIntegrity.hpp"
#include "scene/asset/io/SceneAssetMetaWriter.hpp"

#include <algorithm>
#include <set>
#include <vector>

namespace kb::scene {
namespace {

using SceneAssetBinaryIO::WriteBytesAtomically;
using SceneAssetBinaryIO::WriteFloat;
using SceneAssetBinaryIO::WriteRaw;
using SceneAssetBinaryIO::WriteString;
using SceneAssetBinaryIO::WriteUInt8;
using SceneAssetBinaryIO::WriteUInt32;
using SceneAssetBinaryIO::WriteUInt64;

enum SceneNodeComponentBits : std::uint32_t {
    CameraBit = 1U << 0U,
    MeshRendererBit = 1U << 1U,
    LightBit = 1U << 2U,
    InputBit = 1U << 3U,
    RigidbodyBit = 1U << 4U,
    ColliderBit = 1U << 5U,
};

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

void WriteVec3(std::vector<std::uint8_t>& output, Vec3 value) {
    WriteFloat(output, value.x);
    WriteFloat(output, value.y);
    WriteFloat(output, value.z);
}

void WriteQuat(std::vector<std::uint8_t>& output, Quat value) {
    WriteFloat(output, value.x);
    WriteFloat(output, value.y);
    WriteFloat(output, value.z);
    WriteFloat(output, value.w);
}

void WriteCamera(std::vector<std::uint8_t>& output, const CameraComponent& camera) {
    WriteUInt32(output, static_cast<std::uint32_t>(camera.projection));
    WriteFloat(output, camera.verticalFovDegrees);
    WriteFloat(output, camera.orthographicHeight);
    WriteFloat(output, camera.nearClip);
    WriteFloat(output, camera.farClip);
    WriteUInt8(output, camera.primary ? 1U : 0U);
}

void WriteMeshRenderer(std::vector<std::uint8_t>& output, const MeshRendererComponent& meshRenderer) {
    WriteUInt64(output, meshRenderer.meshAssetId);
    WriteUInt64(output, meshRenderer.materialAssetId);
    WriteUInt32(output, meshRenderer.materialSlotOverrideCount);
    for (const std::uint64_t materialSlotAssetId : meshRenderer.materialSlotAssetIds) {
        WriteUInt64(output, materialSlotAssetId);
    }
    WriteUInt8(output, meshRenderer.castsShadow ? 1U : 0U);
    WriteUInt8(output, meshRenderer.receivesShadow ? 1U : 0U);
}

void WriteLight(std::vector<std::uint8_t>& output, const LightComponent& light) {
    WriteUInt32(output, static_cast<std::uint32_t>(light.kind));
    WriteVec3(output, light.color);
    WriteFloat(output, light.intensity);
    WriteFloat(output, light.range);
    WriteFloat(output, light.innerConeDegrees);
    WriteFloat(output, light.outerConeDegrees);
    WriteFloat(output, light.areaWidth);
    WriteFloat(output, light.areaHeight);
    WriteFloat(output, light.contactShadowLength);
    WriteFloat(output, light.volumetricScattering);
    WriteUInt8(output, light.castsShadow ? 1U : 0U);
}

void WriteInput(std::vector<std::uint8_t>& output, const InputComponent& input) {
    WriteUInt64(output, input.mappingContextAssetId);
    WriteUInt32(output, static_cast<std::uint32_t>(input.priority));
    WriteUInt8(output, input.enabled ? 1U : 0U);
}

void WriteRigidbody(std::vector<std::uint8_t>& output, const RigidbodyComponent& rigidbody) {
    WriteUInt32(output, static_cast<std::uint32_t>(rigidbody.bodyType));
    WriteFloat(output, rigidbody.mass);
    WriteVec3(output, rigidbody.linearVelocity);
    WriteVec3(output, rigidbody.angularVelocity);
    WriteFloat(output, rigidbody.gravityScale);
    WriteUInt8(output, rigidbody.useGravity ? 1U : 0U);
    WriteUInt8(output, rigidbody.lockRotation ? 1U : 0U);
}

void WriteCollider(std::vector<std::uint8_t>& output, const ColliderComponent& collider) {
    WriteUInt32(output, static_cast<std::uint32_t>(collider.shape));
    WriteVec3(output, collider.center);
    WriteVec3(output, collider.boxSize);
    WriteFloat(output, collider.radius);
    WriteFloat(output, collider.height);
    WriteUInt8(output, collider.trigger ? 1U : 0U);
}

void WriteNestedOverride(std::vector<std::uint8_t>& output, const ScenePrefabPropertyOverride& property) {
    WriteUInt32(output, property.nodeIndex);
    WriteUInt32(output, static_cast<std::uint32_t>(property.flag));
    WriteString(output, property.propertyPath);
    WriteString(output, property.value);
}

void WriteNode(std::vector<std::uint8_t>& output, const ScenePrefabNodeDesc& node) {
    WriteString(output, node.name);
    WriteString(output, node.nestedPrefabGuid);
    WriteUInt32(output, static_cast<std::uint32_t>(node.nestedPrefabOverrides.size()));
    for (const ScenePrefabPropertyOverride& property : node.nestedPrefabOverrides) {
        WriteNestedOverride(output, property);
    }
    WriteUInt32(output, node.parentNode);
    WriteVec3(output, node.transform.localPosition);
    WriteQuat(output, node.transform.localRotation);
    WriteVec3(output, node.transform.localScale);
    WriteUInt8(output, node.visibility.visible ? 1U : 0U);

    std::uint32_t componentBits = 0;
    componentBits |= node.components.camera.has_value() ? CameraBit : 0U;
    componentBits |= node.components.meshRenderer.has_value() ? MeshRendererBit : 0U;
    componentBits |= node.components.light.has_value() ? LightBit : 0U;
    componentBits |= node.components.input.has_value() ? InputBit : 0U;
    componentBits |= node.components.rigidbody.has_value() ? RigidbodyBit : 0U;
    componentBits |= node.components.collider.has_value() ? ColliderBit : 0U;
    WriteUInt32(output, componentBits);
    if (node.components.camera.has_value()) {
        WriteCamera(output, *node.components.camera);
    }
    if (node.components.meshRenderer.has_value()) {
        WriteMeshRenderer(output, *node.components.meshRenderer);
    }
    if (node.components.light.has_value()) {
        WriteLight(output, *node.components.light);
    }
    if (node.components.input.has_value()) {
        WriteInput(output, *node.components.input);
    }
    if (node.components.rigidbody.has_value()) {
        WriteRigidbody(output, *node.components.rigidbody);
    }
    if (node.components.collider.has_value()) {
        WriteCollider(output, *node.components.collider);
    }
}

void AddDependency(std::vector<SceneAssetDependency>& dependencies, std::set<std::uint64_t>& seen, std::uint64_t rawId, std::string role) {
    if (rawId == 0U || seen.contains(rawId)) {
        return;
    }
    seen.insert(rawId);
    dependencies.push_back(SceneAssetDependency{
        .assetId = kb::assets::AssetId{ rawId },
        .role = std::move(role),
    });
}

[[nodiscard]] std::vector<SceneAssetDependency> CollectDependencies(const ScenePrefab& prefab) {
    std::vector<SceneAssetDependency> dependencies;
    std::set<std::uint64_t> seen;
    for (const ScenePrefabNodeDesc& node : prefab.Nodes()) {
        if (node.components.meshRenderer.has_value()) {
            AddDependency(dependencies, seen, node.components.meshRenderer->meshAssetId, "mesh");
            AddDependency(dependencies, seen, node.components.meshRenderer->materialAssetId, "material");
            for (std::uint32_t slot = 0U; slot < node.components.meshRenderer->materialSlotOverrideCount && slot < kMaxMeshRendererMaterialSlotOverrides; ++slot) {
                AddDependency(dependencies, seen, node.components.meshRenderer->materialSlotAssetIds[slot], "materialSlot");
            }
        }
        if (!node.nestedPrefabGuid.empty()) {
            const kb::assets::AssetId nestedId = kb::assets::MakeAssetId(node.nestedPrefabGuid);
            AddDependency(dependencies, seen, nestedId.value, "nestedPrefab");
        }
    }
    std::ranges::sort(dependencies, [](const SceneAssetDependency& left, const SceneAssetDependency& right) {
        return left.assetId.value < right.assetId.value;
    });
    return dependencies;
}

[[nodiscard]] bool CanWrite(const SceneDocument& scene) {
    return !scene.name.empty() &&
        !scene.guid.empty() &&
        scene.worldPrefab.NodeCount() <= SceneAssetFormat::MaxNodeCount;
}

[[nodiscard]] std::vector<std::uint8_t> Serialize(const SceneDocument& scene) {
    std::vector<std::uint8_t> output;
    output.reserve(256U + scene.worldPrefab.NodeCount() * 128U);
    WriteRaw(output, SceneAssetFormat::Magic.data(), SceneAssetFormat::Magic.size());
    WriteUInt32(output, SceneDocument::CurrentFileVersion);
    WriteString(output, scene.guid);
    WriteString(output, scene.name);
    WriteString(output, scene.worldType);
    WriteUInt32(output, RootCount(scene.worldPrefab));
    WriteUInt32(output, static_cast<std::uint32_t>(scene.worldPrefab.NodeCount()));
    for (const ScenePrefabNodeDesc& node : scene.worldPrefab.Nodes()) {
        WriteNode(output, node);
    }
    return output;
}

} // namespace

bool SceneAssetWriter::Write(const std::filesystem::path& path, const SceneDocument& scene) {
    if (path.extension() != SceneAssetFormat::Extension || !CanWrite(scene)) {
        return false;
    }

    const std::vector<std::uint8_t> bytes = Serialize(scene);
    if (!WriteBytesAtomically(path, bytes)) {
        return false;
    }

    const SceneAssetIntegrity integrity = SceneAssetIntegrityService::ComputeFile(path);
    SceneAssetMeta meta{
        .fileVersion = SceneAssetMeta::CurrentFileVersion,
        .sceneGuid = scene.guid,
        .sceneName = scene.name,
        .worldType = scene.worldType,
        .sceneFile = path.filename(),
        .byteSize = integrity.byteSize,
        .contentHashFnv1a64 = integrity.contentHashFnv1a64,
        .contentChecksumCrc32 = integrity.contentChecksumCrc32,
        .rootCount = RootCount(scene.worldPrefab),
        .nodeCount = static_cast<std::uint32_t>(scene.worldPrefab.NodeCount()),
        .dependencies = CollectDependencies(scene.worldPrefab),
    };
    return SceneAssetMetaWriter::Write(MetaPathFor(path), meta);
}

} // namespace kb::scene
