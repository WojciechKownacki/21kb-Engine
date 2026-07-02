#include "engine/project/ProjectDescriptorWriter.hpp"

#include "engine/project/ProjectDescriptorMeta.hpp"
#include "project/ProjectDescriptorBinaryIO.hpp"
#include "project/ProjectDescriptorFormat.hpp"
#include "project/ProjectDescriptorIntegrity.hpp"
#include "project/ProjectDescriptorMetaWriter.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace kb::project {
namespace {

using ProjectDescriptorBinaryIO::WriteBool;
using ProjectDescriptorBinaryIO::WriteBytesAtomically;
using ProjectDescriptorBinaryIO::WriteRaw;
using ProjectDescriptorBinaryIO::WriteString;
using ProjectDescriptorBinaryIO::WriteUInt32;

[[nodiscard]] std::filesystem::path MetaPathFor(const std::filesystem::path& projectPath) {
    std::filesystem::path metaPath = projectPath;
    metaPath.replace_extension(".meta");
    return metaPath;
}

[[nodiscard]] bool StringFits(std::string_view value) noexcept {
    return value.size() <= ProjectDescriptorFormat::MaxStringBytes;
}

[[nodiscard]] std::uint32_t SceneLightingPathValue(ProjectSceneLightingPath path) noexcept {
    switch (path) {
    case ProjectSceneLightingPath::Deferred:
        return 1U;
    case ProjectSceneLightingPath::ForwardPlus:
        return 2U;
    case ProjectSceneLightingPath::Forward:
    default:
        return 0U;
    }
}

[[nodiscard]] bool CanWrite(const ProjectDescriptor& descriptor) {
    if (descriptor.name.empty() ||
        descriptor.engineAssociation.empty() ||
        descriptor.contentRoot.empty() ||
        descriptor.defaultScene.empty() ||
        descriptor.targetPlatforms.size() > ProjectDescriptorFormat::MaxTargetPlatformCount ||
        descriptor.modules.size() > ProjectDescriptorFormat::MaxModuleCount ||
        descriptor.plugins.size() > ProjectDescriptorFormat::MaxPluginCount) {
        return false;
    }

    if (!StringFits(descriptor.engineAssociation) ||
        !StringFits(descriptor.name) ||
        !StringFits(descriptor.category) ||
        !StringFits(descriptor.description) ||
        !StringFits(descriptor.contentRoot) ||
        !StringFits(descriptor.defaultScene) ||
        !StringFits(descriptor.inputMappingContext)) {
        return false;
    }

    for (const std::string& platform : descriptor.targetPlatforms) {
        if (platform.empty() || !StringFits(platform)) {
            return false;
        }
    }
    for (const ProjectModuleDescriptor& module : descriptor.modules) {
        if (module.name.empty() || module.type.empty() || module.loadingPhase.empty() ||
            !StringFits(module.name) || !StringFits(module.type) || !StringFits(module.loadingPhase)) {
            return false;
        }
    }
    for (const ProjectPluginReference& plugin : descriptor.plugins) {
        if (plugin.name.empty() || !StringFits(plugin.name) || !StringFits(plugin.binaryPath)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::vector<std::uint8_t> Serialize(const ProjectDescriptor& descriptor) {
    std::vector<std::uint8_t> output;
    output.reserve(256U + descriptor.targetPlatforms.size() * 24U + descriptor.modules.size() * 72U + descriptor.plugins.size() * 32U);
    WriteRaw(output, ProjectDescriptorFormat::Magic.data(), ProjectDescriptorFormat::Magic.size());
    WriteUInt32(output, ProjectDescriptor::CurrentFileVersion);
    WriteString(output, descriptor.engineAssociation);
    WriteString(output, descriptor.name);
    WriteString(output, descriptor.category);
    WriteString(output, descriptor.description);
    WriteString(output, descriptor.contentRoot);
    WriteString(output, descriptor.defaultScene);
    WriteBool(output, descriptor.disableEnginePluginsByDefault);

    WriteUInt32(output, static_cast<std::uint32_t>(descriptor.targetPlatforms.size()));
    for (const std::string& platform : descriptor.targetPlatforms) {
        WriteString(output, platform);
    }

    WriteUInt32(output, static_cast<std::uint32_t>(descriptor.modules.size()));
    for (const ProjectModuleDescriptor& module : descriptor.modules) {
        WriteString(output, module.name);
        WriteString(output, module.type);
        WriteString(output, module.loadingPhase);
    }

    WriteUInt32(output, static_cast<std::uint32_t>(descriptor.plugins.size()));
    for (const ProjectPluginReference& plugin : descriptor.plugins) {
        WriteString(output, plugin.name);
        WriteString(output, plugin.binaryPath);
        WriteBool(output, plugin.enabled);
    }

    // File version 2+: project-wide input settings.
    WriteString(output, descriptor.inputMappingContext);
    WriteBool(output, descriptor.inputEnabled);
    // File version 4+: project-wide scene lighting path.
    WriteUInt32(output, SceneLightingPathValue(descriptor.sceneLightingPath));
    return output;
}

} // namespace

bool ProjectDescriptorWriter::Write(const std::filesystem::path& path, const ProjectDescriptor& descriptor) {
    if (!CanWrite(descriptor)) {
        return false;
    }

    const std::vector<std::uint8_t> bytes = Serialize(descriptor);
    if (!WriteBytesAtomically(path, bytes)) {
        return false;
    }

    const ProjectDescriptorIntegrity integrity = ProjectDescriptorIntegrityService::ComputeFile(path);
    const ProjectDescriptorMeta meta{
        .fileVersion = ProjectDescriptorMeta::CurrentFileVersion,
        .projectName = descriptor.name,
        .engineAssociation = descriptor.engineAssociation,
        .defaultScene = descriptor.defaultScene,
        .projectFile = path.filename(),
        .byteSize = integrity.byteSize,
        .contentHashFnv1a64 = integrity.contentHashFnv1a64,
        .contentChecksumCrc32 = integrity.contentChecksumCrc32,
        .targetPlatformCount = static_cast<std::uint32_t>(descriptor.targetPlatforms.size()),
        .moduleCount = static_cast<std::uint32_t>(descriptor.modules.size()),
        .pluginCount = static_cast<std::uint32_t>(descriptor.plugins.size()),
    };
    return ProjectDescriptorMetaWriter::Write(MetaPathFor(path), meta);
}

} // namespace kb::project
