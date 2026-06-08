#include "scene/asset/io/SceneAssetMetaReader.hpp"

#include "scene/asset/io/SceneAssetBinaryIO.hpp"
#include "scene/asset/io/SceneAssetFormat.hpp"

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace kb::scene {
namespace {

using SceneAssetBinaryIO::ByteReader;
using SceneAssetBinaryIO::ReadAllBytes;

[[nodiscard]] bool ReadDependency(ByteReader& input, SceneAssetDependency& dependency) {
    return input.ReadUInt64(dependency.assetId.value) &&
        input.ReadString(dependency.role) &&
        dependency.assetId.IsValid() &&
        !dependency.role.empty();
}

} // namespace

SceneAssetMetaReadResult SceneAssetMetaReader::Read(const std::filesystem::path& path) {
    std::vector<std::uint8_t> bytes = ReadAllBytes(path);
    if (bytes.empty()) {
        return SceneAssetMetaReadResult{ .succeeded = false, .meta = {}, .error = "Scene meta could not be opened." };
    }

    ByteReader input{ std::move(bytes) };
    std::array<std::uint8_t, SceneAssetFormat::MetaMagic.size()> magic{};
    SceneAssetMeta meta;
    std::string sceneFile;
    std::uint32_t dependencyCount = 0;
    if (!input.ReadRaw(magic.data(), magic.size()) ||
        magic != SceneAssetFormat::MetaMagic ||
        !input.ReadUInt32(meta.fileVersion) ||
        meta.fileVersion == 0U ||
        meta.fileVersion > SceneAssetMeta::CurrentFileVersion ||
        !input.ReadString(meta.sceneGuid) ||
        !input.ReadString(meta.sceneName) ||
        !input.ReadString(meta.worldType) ||
        !input.ReadString(sceneFile) ||
        !input.ReadUInt64(meta.byteSize) ||
        !input.ReadUInt64(meta.contentHashFnv1a64) ||
        !input.ReadUInt32(meta.contentChecksumCrc32) ||
        !input.ReadUInt32(meta.rootCount) ||
        !input.ReadUInt32(meta.nodeCount) ||
        !input.ReadUInt32(dependencyCount) ||
        dependencyCount > SceneAssetFormat::MaxDependencyCount) {
        return SceneAssetMetaReadResult{ .succeeded = false, .meta = {}, .error = "Scene meta descriptor fields are invalid." };
    }
    meta.sceneFile = std::move(sceneFile);

    meta.dependencies.reserve(dependencyCount);
    for (std::uint32_t index = 0U; index < dependencyCount; ++index) {
        SceneAssetDependency dependency;
        if (!ReadDependency(input, dependency)) {
            return SceneAssetMetaReadResult{ .succeeded = false, .meta = {}, .error = "Scene meta dependency fields are invalid." };
        }
        meta.dependencies.push_back(std::move(dependency));
    }

    if (!input.Exhausted()) {
        return SceneAssetMetaReadResult{ .succeeded = false, .meta = {}, .error = "Scene meta contains trailing data." };
    }

    return SceneAssetMetaReadResult{ .succeeded = true, .meta = std::move(meta), .error = {} };
}

} // namespace kb::scene
