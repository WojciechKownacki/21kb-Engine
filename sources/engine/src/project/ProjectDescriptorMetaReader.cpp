#include "project/ProjectDescriptorMetaReader.hpp"

#include "project/ProjectDescriptorBinaryIO.hpp"
#include "project/ProjectDescriptorFormat.hpp"

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace kb::project {
namespace {

using ProjectDescriptorBinaryIO::ByteReader;
using ProjectDescriptorBinaryIO::ReadAllBytes;

} // namespace

ProjectDescriptorMetaReadResult ProjectDescriptorMetaReader::Read(const std::filesystem::path& path) {
    std::vector<std::uint8_t> bytes = ReadAllBytes(path);
    if (bytes.empty()) {
        return ProjectDescriptorMetaReadResult{ .succeeded = false, .meta = {}, .error = "Project meta could not be opened." };
    }

    ByteReader input{ std::move(bytes) };
    std::array<std::uint8_t, ProjectDescriptorFormat::MetaMagic.size()> magic{};
    ProjectDescriptorMeta meta;
    std::string projectFile;
    if (!input.ReadRaw(magic.data(), magic.size()) ||
        magic != ProjectDescriptorFormat::MetaMagic ||
        !input.ReadUInt32(meta.fileVersion) ||
        meta.fileVersion == 0U ||
        meta.fileVersion > ProjectDescriptorMeta::CurrentFileVersion ||
        !input.ReadString(meta.projectName) ||
        !input.ReadString(meta.engineAssociation) ||
        !input.ReadString(meta.defaultScene) ||
        !input.ReadString(projectFile) ||
        !input.ReadUInt64(meta.byteSize) ||
        !input.ReadUInt64(meta.contentHashFnv1a64) ||
        !input.ReadUInt32(meta.contentChecksumCrc32) ||
        !input.ReadUInt32(meta.targetPlatformCount) ||
        !input.ReadUInt32(meta.moduleCount) ||
        !input.ReadUInt32(meta.pluginCount) ||
        meta.targetPlatformCount > ProjectDescriptorFormat::MaxTargetPlatformCount ||
        meta.moduleCount > ProjectDescriptorFormat::MaxModuleCount ||
        meta.pluginCount > ProjectDescriptorFormat::MaxPluginCount) {
        return ProjectDescriptorMetaReadResult{ .succeeded = false, .meta = {}, .error = "Project meta descriptor fields are invalid." };
    }
    meta.projectFile = std::move(projectFile);

    if (!input.Exhausted()) {
        return ProjectDescriptorMetaReadResult{ .succeeded = false, .meta = {}, .error = "Project meta contains trailing data." };
    }
    if (meta.projectName.empty() || meta.engineAssociation.empty() || meta.projectFile.empty() ||
        meta.byteSize == 0U || meta.contentHashFnv1a64 == 0U) {
        return ProjectDescriptorMetaReadResult{ .succeeded = false, .meta = {}, .error = "Project meta summary is incomplete." };
    }

    return ProjectDescriptorMetaReadResult{ .succeeded = true, .meta = std::move(meta), .error = {} };
}

} // namespace kb::project
