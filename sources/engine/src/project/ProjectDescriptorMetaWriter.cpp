#include "project/ProjectDescriptorMetaWriter.hpp"

#include "project/ProjectDescriptorBinaryIO.hpp"
#include "project/ProjectDescriptorFormat.hpp"

#include <vector>

namespace kb::project {
namespace {

using ProjectDescriptorBinaryIO::WriteBytesAtomically;
using ProjectDescriptorBinaryIO::WriteRaw;
using ProjectDescriptorBinaryIO::WriteString;
using ProjectDescriptorBinaryIO::WriteUInt32;
using ProjectDescriptorBinaryIO::WriteUInt64;

[[nodiscard]] bool CanWrite(const ProjectDescriptorMeta& meta) {
    return !meta.projectName.empty() &&
        !meta.engineAssociation.empty() &&
        !meta.projectFile.empty() &&
        meta.byteSize != 0U &&
        meta.contentHashFnv1a64 != 0U &&
        meta.targetPlatformCount <= ProjectDescriptorFormat::MaxTargetPlatformCount &&
        meta.moduleCount <= ProjectDescriptorFormat::MaxModuleCount &&
        meta.pluginCount <= ProjectDescriptorFormat::MaxPluginCount;
}

[[nodiscard]] std::vector<std::uint8_t> Serialize(const ProjectDescriptorMeta& meta) {
    std::vector<std::uint8_t> output;
    output.reserve(160U);
    WriteRaw(output, ProjectDescriptorFormat::MetaMagic.data(), ProjectDescriptorFormat::MetaMagic.size());
    WriteUInt32(output, ProjectDescriptorMeta::CurrentFileVersion);
    WriteString(output, meta.projectName);
    WriteString(output, meta.engineAssociation);
    WriteString(output, meta.defaultScene);
    WriteString(output, meta.projectFile.generic_string());
    WriteUInt64(output, meta.byteSize);
    WriteUInt64(output, meta.contentHashFnv1a64);
    WriteUInt32(output, meta.contentChecksumCrc32);
    WriteUInt32(output, meta.targetPlatformCount);
    WriteUInt32(output, meta.moduleCount);
    WriteUInt32(output, meta.pluginCount);
    return output;
}

} // namespace

bool ProjectDescriptorMetaWriter::Write(const std::filesystem::path& path, const ProjectDescriptorMeta& meta) {
    return CanWrite(meta) && WriteBytesAtomically(path, Serialize(meta));
}

} // namespace kb::project
