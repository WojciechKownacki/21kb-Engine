#include "scene/asset/io/SceneAssetReader.hpp"

#include "engine/scene/SceneAssetMeta.hpp"
#include "scene/asset/io/SceneAssetBinaryIO.hpp"
#include "scene/asset/io/SceneAssetComponentCodec.hpp"
#include "scene/asset/io/SceneAssetFormat.hpp"
#include "scene/asset/io/SceneAssetIntegrity.hpp"
#include "scene/asset/io/SceneAssetMetaReader.hpp"
#include "scene/asset/io/SceneAssetPrimitiveCodec.hpp"
#include "scene/document/SceneDocumentAudioValidation.hpp"
#include "scene/prefab/ScenePrefabValidator.hpp"

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace kb::scene {
namespace {

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

[[nodiscard]] bool ReadNode(ByteReader& input, std::uint32_t fileVersion, ScenePrefabNodeDesc& output) {
    std::uint32_t nestedOverrideCount = 0;
    bool visible = true;
    std::uint8_t rawMode = static_cast<std::uint8_t>(VisibilityMode::Visible);
    std::uint32_t mask = VisibilityComponent::AllMask;
    if ((fileVersion >= 4U && !input.ReadUInt64(output.stableId)) ||
        !input.ReadString(output.name) ||
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

    if (!input.ReadUInt32(output.parentNode) ||
        !SceneAssetPrimitiveCodec::ReadVec3(input, output.transform.localPosition) ||
        !SceneAssetPrimitiveCodec::ReadQuat(input, output.transform.localRotation) ||
        !SceneAssetPrimitiveCodec::ReadVec3(input, output.transform.localScale) ||
        !input.ReadBool(visible) ||
        (fileVersion >= 9U && (!input.ReadUInt8(rawMode) || !input.ReadUInt32(mask))) ||
        !SceneAssetComponentCodec::Read(input, fileVersion, output.components)) {
        return false;
    }
    output.visibility.mode = fileVersion >= 9U
        ? static_cast<VisibilityMode>(rawMode)
        : (visible ? VisibilityMode::Visible : VisibilityMode::Hidden);
    if (!IsVisibilityModeValid(output.visibility.mode)) return false;
    output.visibility.mask = mask;
    output.visibility.visible = output.visibility.mode != VisibilityMode::Hidden;
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
        !input.ReadString(scene.worldType)) {
        return SceneDocumentLoadResult{ .succeeded = false, .document = {}, .error = "Scene asset descriptor fields are invalid." };
    }
    if (fileVersion >= 27U) {
        std::uint32_t tagCount = 0U;
        if (!input.ReadUInt32(tagCount) || tagCount > 256U) {
            return SceneDocumentLoadResult{ .succeeded = false, .document = {}, .error = "Scene asset tag catalog is invalid." };
        }
        scene.tagDefinitions.clear();
        scene.tagDefinitions.reserve(tagCount);
        for (std::uint32_t tagIndex = 0U; tagIndex < tagCount; ++tagIndex) {
            std::string tag;
            if (!input.ReadString(tag) || tag.empty() || tag.size() > 255U) {
                return SceneDocumentLoadResult{ .succeeded = false, .document = {}, .error = "Scene asset tag catalog is invalid." };
            }
            scene.tagDefinitions.push_back(std::move(tag));
        }
    }
    if (!input.ReadUInt32(rootCount) ||
        !input.ReadUInt32(nodeCount) ||
        nodeCount > SceneAssetFormat::MaxNodeCount) {
        return SceneDocumentLoadResult{ .succeeded = false, .document = {}, .error = "Scene asset descriptor fields are invalid." };
    }
    scene.fileVersion = fileVersion;
    scene.worldPrefab.Reserve(nodeCount);
    for (std::uint32_t nodeIndex = 0U; nodeIndex < nodeCount; ++nodeIndex) {
        ScenePrefabNodeDesc node;
        if (!ReadNode(input, fileVersion, node)) {
            return SceneDocumentLoadResult{ .succeeded = false, .document = {}, .error = "Scene asset node list is invalid." };
        }
        static_cast<void>(scene.worldPrefab.AddNode(std::move(node)));
    }

    if (fileVersion >= 32U) {
        bool occlusionEnabled = false;
        if (!input.ReadUInt64(scene.audioMixerAssetId)
            || !input.ReadString(scene.audioMixerSnapshot, static_cast<std::uint32_t>(kb::audio::kMaxAudioMixerNameBytes))
            || !input.ReadBool(occlusionEnabled)
            || !input.ReadFloat(scene.audioOcclusionSettings.occludedVolumeScale)
            || !input.ReadFloat(scene.audioOcclusionSettings.maxDistance)
            || !input.ReadUInt32(scene.audioOcclusionSettings.layerMask)
            || !input.ReadUInt32(scene.audioOcclusionSettings.maxRaycastsPerTick)) {
            return SceneDocumentLoadResult{ .succeeded = false, .document = {}, .error = "Scene asset audio configuration is invalid." };
        }
        scene.audioOcclusionSettings.enabled = occlusionEnabled;
    }

    if (!input.Exhausted()) {
        return SceneDocumentLoadResult{ .succeeded = false, .document = {}, .error = "Scene asset contains trailing data." };
    }
    if (!IsSceneDocumentAudioConfigurationValid(scene)) {
        return SceneDocumentLoadResult{ .succeeded = false, .document = {}, .error = "Scene asset audio configuration is invalid." };
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
