#include "scene/asset/io/SceneAssetWriter.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/scene/SceneAssetMeta.hpp"
#include "scene/asset/io/SceneAssetBinaryIO.hpp"
#include "scene/asset/io/SceneAssetComponentCodec.hpp"
#include "scene/asset/io/SceneAssetFormat.hpp"
#include "scene/asset/io/SceneAssetIntegrity.hpp"
#include "scene/asset/io/SceneAssetMetaWriter.hpp"
#include "scene/asset/io/SceneAssetPrimitiveCodec.hpp"
#include "scene/prefab/ScenePrefabValidator.hpp"

#include <algorithm>
#include <set>
#include <vector>

namespace kb::scene {
namespace {

using SceneAssetBinaryIO::WriteBytesAtomically;
using SceneAssetBinaryIO::WriteRaw;
using SceneAssetBinaryIO::WriteString;
using SceneAssetBinaryIO::WriteUInt8;
using SceneAssetBinaryIO::WriteUInt32;

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

void WriteNestedOverride(std::vector<std::uint8_t>& output, const ScenePrefabPropertyOverride& property) {
    WriteUInt32(output, property.nodeIndex);
    WriteUInt32(output, static_cast<std::uint32_t>(property.flag));
    WriteString(output, property.propertyPath);
    WriteString(output, property.value);
}

void WriteNode(std::vector<std::uint8_t>& output, const ScenePrefabNodeDesc& node) {
    SceneAssetBinaryIO::WriteUInt64(output, node.stableId);
    WriteString(output, node.name);
    WriteString(output, node.nestedPrefabGuid);
    WriteUInt32(output, static_cast<std::uint32_t>(node.nestedPrefabOverrides.size()));
    for (const ScenePrefabPropertyOverride& property : node.nestedPrefabOverrides) {
        WriteNestedOverride(output, property);
    }
    WriteUInt32(output, node.parentNode);
    SceneAssetPrimitiveCodec::WriteVec3(output, node.transform.localPosition);
    SceneAssetPrimitiveCodec::WriteQuat(output, node.transform.localRotation);
    SceneAssetPrimitiveCodec::WriteVec3(output, node.transform.localScale);
    // Prefab descriptors authored against the pre-v2 visibility API can
    // still set only `visible = false`. Serialize that compatibility value
    // canonically as the v2 Hidden gate; otherwise a default Visible mode
    // would revive hidden legacy content after a save/load round trip.
    const VisibilityMode visibilityMode = node.visibility.visible
        ? node.visibility.mode : VisibilityMode::Hidden;
    WriteUInt8(output, visibilityMode != VisibilityMode::Hidden ? 1U : 0U);
    WriteUInt8(output, static_cast<std::uint8_t>(visibilityMode));
    WriteUInt32(output, node.visibility.mask);
    SceneAssetComponentCodec::Write(output, node.components);
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
        if (node.components.audioSource.has_value()) {
            AddDependency(dependencies, seen, node.components.audioSource->clipAssetId, "audioClip");
        }
        if (node.components.behaviour.has_value()) {
            AddDependency(dependencies, seen, node.components.behaviour->behaviourAssetId, "behaviour");
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
        scene.worldPrefab.NodeCount() <= SceneAssetFormat::MaxNodeCount &&
        ScenePrefabValidator::IsValid(scene.worldPrefab);
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
