#include "scene/asset/io/SceneAssetMetaWriter.hpp"

#include "scene/asset/io/SceneAssetBinaryIO.hpp"
#include "scene/asset/io/SceneAssetFormat.hpp"

#include <vector>

namespace kb::scene {
namespace {

using SceneAssetBinaryIO::WriteBytesAtomically;
using SceneAssetBinaryIO::WriteRaw;
using SceneAssetBinaryIO::WriteString;
using SceneAssetBinaryIO::WriteUInt32;
using SceneAssetBinaryIO::WriteUInt64;

[[nodiscard]] std::vector<std::uint8_t> Serialize(const SceneAssetMeta& meta) {
    std::vector<std::uint8_t> output;
    output.reserve(128U + meta.dependencies.size() * 32U);
    WriteRaw(output, SceneAssetFormat::MetaMagic.data(), SceneAssetFormat::MetaMagic.size());
    WriteUInt32(output, SceneAssetMeta::CurrentFileVersion);
    WriteString(output, meta.sceneGuid);
    WriteString(output, meta.sceneName);
    WriteString(output, meta.worldType);
    WriteString(output, meta.sceneFile.generic_string());
    WriteUInt64(output, meta.byteSize);
    WriteUInt64(output, meta.contentHashFnv1a64);
    WriteUInt32(output, meta.contentChecksumCrc32);
    WriteUInt32(output, meta.rootCount);
    WriteUInt32(output, meta.nodeCount);
    WriteUInt32(output, static_cast<std::uint32_t>(meta.dependencies.size()));
    for (const SceneAssetDependency& dependency : meta.dependencies) {
        WriteUInt64(output, dependency.assetId.value);
        WriteString(output, dependency.role);
    }
    return output;
}

[[nodiscard]] bool CanWrite(const SceneAssetMeta& meta) {
    return !meta.sceneGuid.empty() &&
        !meta.sceneName.empty() &&
        meta.byteSize != 0U &&
        meta.contentHashFnv1a64 != 0U &&
        meta.dependencies.size() <= SceneAssetFormat::MaxDependencyCount;
}

} // namespace

bool SceneAssetMetaWriter::Write(const std::filesystem::path& path, const SceneAssetMeta& meta) {
    return CanWrite(meta) && WriteBytesAtomically(path, Serialize(meta));
}

} // namespace kb::scene
