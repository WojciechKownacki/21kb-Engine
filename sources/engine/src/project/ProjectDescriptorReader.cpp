#include "engine/project/ProjectDescriptorReader.hpp"

#include "project/ProjectDescriptorBinaryIO.hpp"
#include "project/ProjectDescriptorFormat.hpp"
#include "project/ProjectDescriptorIntegrity.hpp"
#include "project/ProjectDescriptorMetaReader.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace kb::project {
namespace {

using ProjectDescriptorBinaryIO::ByteReader;
using ProjectDescriptorBinaryIO::ReadAllBytes;

[[nodiscard]] std::filesystem::path MetaPathFor(const std::filesystem::path& projectPath) {
    std::filesystem::path metaPath = projectPath;
    metaPath.replace_extension(".meta");
    return metaPath;
}

[[nodiscard]] bool ReadStringList(ByteReader& input, std::uint32_t maxCount, std::vector<std::string>& output) {
    std::uint32_t count = 0U;
    if (!input.ReadUInt32(count) || count > maxCount) {
        return false;
    }
    output.clear();
    output.reserve(count);
    for (std::uint32_t index = 0U; index < count; ++index) {
        std::string value;
        if (!input.ReadString(value) || value.empty()) {
            return false;
        }
        output.push_back(std::move(value));
    }
    return true;
}

[[nodiscard]] bool ReadModules(ByteReader& input, ProjectDescriptor& descriptor) {
    std::uint32_t count = 0U;
    if (!input.ReadUInt32(count) || count > ProjectDescriptorFormat::MaxModuleCount) {
        return false;
    }
    descriptor.modules.clear();
    descriptor.modules.reserve(count);
    for (std::uint32_t index = 0U; index < count; ++index) {
        ProjectModuleDescriptor module;
        if (!input.ReadString(module.name) ||
            !input.ReadString(module.type) ||
            !input.ReadString(module.loadingPhase) ||
            module.name.empty() ||
            module.type.empty() ||
            module.loadingPhase.empty()) {
            return false;
        }
        descriptor.modules.push_back(std::move(module));
    }
    return true;
}

[[nodiscard]] bool ReadPlugins(ByteReader& input, ProjectDescriptor& descriptor, std::uint32_t fileVersion) {
    std::uint32_t count = 0U;
    if (!input.ReadUInt32(count) || count > ProjectDescriptorFormat::MaxPluginCount) {
        return false;
    }
    descriptor.plugins.clear();
    descriptor.plugins.reserve(count);
    for (std::uint32_t index = 0U; index < count; ++index) {
        ProjectPluginReference plugin;
        if (!input.ReadString(plugin.name) ||
            plugin.name.empty()) {
            return false;
        }
        if (fileVersion >= 3U && !input.ReadString(plugin.binaryPath)) {
            return false;
        }
        if (!input.ReadBool(plugin.enabled)) {
            return false;
        }
        descriptor.plugins.push_back(std::move(plugin));
    }
    return true;
}

[[nodiscard]] bool ReadSceneLightingPath(ByteReader& input, ProjectLegacySettings& legacy) {
    std::uint32_t value = 0U;
    if (!input.ReadUInt32(value)) {
        return false;
    }
    switch (value) {
    case 0U:
        legacy.lightingPath = ProjectSceneLightingPath::Forward;
        return true;
    case 1U:
        legacy.lightingPath = ProjectSceneLightingPath::Deferred;
        return true;
    case 2U:
        legacy.lightingPath = ProjectSceneLightingPath::ForwardPlus;
        return true;
    default:
        return false;
    }
}

[[nodiscard]] ProjectDescriptorReadResult ValidateMeta(const std::filesystem::path& path, const ProjectDescriptor& descriptor) {
    const ProjectDescriptorMetaReadResult metaResult = ProjectDescriptorMetaReader::Read(MetaPathFor(path));
    if (!metaResult.succeeded) {
        return ProjectDescriptorReadResult{ .succeeded = false, .descriptor = {}, .error = metaResult.error };
    }

    const ProjectDescriptorIntegrity integrity = ProjectDescriptorIntegrityService::ComputeFile(path);
    const ProjectDescriptorMeta& meta = metaResult.meta;
    if (meta.contentHashFnv1a64 != integrity.contentHashFnv1a64 ||
        meta.contentChecksumCrc32 != integrity.contentChecksumCrc32 ||
        meta.byteSize != integrity.byteSize) {
        return ProjectDescriptorReadResult{ .succeeded = false, .descriptor = {}, .error = "Project descriptor integrity does not match its .meta file." };
    }
    if (meta.engineAssociation != descriptor.engineAssociation ||
        meta.targetPlatformCount != descriptor.targetPlatforms.size() ||
        meta.moduleCount != descriptor.modules.size() ||
        meta.pluginCount != descriptor.plugins.size()) {
        return ProjectDescriptorReadResult{ .succeeded = false, .descriptor = {}, .error = "Project descriptor summary does not match its .meta file." };
    }
    if (meta.projectFile.filename() != path.filename()) {
        return ProjectDescriptorReadResult{ .succeeded = false, .descriptor = {}, .error = "Project descriptor .meta points to a different project file." };
    }
    return ProjectDescriptorReadResult{ .succeeded = true, .descriptor = descriptor, .error = {} };
}

} // namespace

ProjectDescriptorReadResult ProjectDescriptorReader::Read(const std::filesystem::path& path) {
    std::vector<std::uint8_t> bytes = ReadAllBytes(path);
    if (bytes.empty()) {
        return ProjectDescriptorReadResult{ .succeeded = false, .descriptor = {}, .error = "Project descriptor could not be opened." };
    }

    ByteReader input{ std::move(bytes) };
    std::array<std::uint8_t, ProjectDescriptorFormat::Magic.size()> magic{};
    std::uint32_t fileVersion = 0U;
    if (!input.ReadRaw(magic.data(), magic.size()) ||
        magic != ProjectDescriptorFormat::Magic ||
        !input.ReadUInt32(fileVersion) ||
        fileVersion == 0U ||
        fileVersion > ProjectDescriptor::CurrentFileVersion) {
        return ProjectDescriptorReadResult{ .succeeded = false, .descriptor = {}, .error = "Project descriptor header is invalid." };
    }

    ProjectDescriptor descriptor;
    ProjectLegacySettings legacy;
    legacy.present = fileVersion < 6U;
    descriptor.fileVersion = fileVersion;
    if (!input.ReadString(descriptor.engineAssociation) ||
        (legacy.present && !input.ReadString(legacy.name)) ||
        (legacy.present && !input.ReadString(legacy.category)) ||
        (legacy.present && !input.ReadString(legacy.description)) ||
        !input.ReadString(descriptor.contentRoot) ||
        (legacy.present && !input.ReadString(legacy.defaultScene)) ||
        !input.ReadBool(descriptor.disableEnginePluginsByDefault) ||
        !ReadStringList(input, ProjectDescriptorFormat::MaxTargetPlatformCount, descriptor.targetPlatforms) ||
        !ReadModules(input, descriptor) ||
        !ReadPlugins(input, descriptor, fileVersion)) {
        return ProjectDescriptorReadResult{ .succeeded = false, .descriptor = {}, .error = "Project descriptor fields are invalid." };
    }

    // Settings a file written before version 6 still carries. They are read so the
    // project can hand them to its settings file, not so the descriptor keeps them.
    if (fileVersion >= 2U && fileVersion < 6U) {
        if (!input.ReadString(legacy.inputMappingContext) || !input.ReadBool(legacy.inputEnabled)) {
            return ProjectDescriptorReadResult{ .succeeded = false, .descriptor = {}, .error = "Project descriptor input settings are invalid." };
        }
    }
    if (fileVersion >= 4U && fileVersion < 6U && !ReadSceneLightingPath(input, legacy)) {
        return ProjectDescriptorReadResult{ .succeeded = false, .descriptor = {}, .error = "Project descriptor scene lighting path is invalid." };
    }
    if (fileVersion >= 5U && fileVersion < 6U && !input.ReadString(legacy.physicsLayersAsset)) {
        return ProjectDescriptorReadResult{ .succeeded = false, .descriptor = {}, .error = "Project descriptor physics layers asset is invalid." };
    }

    if (!input.Exhausted()) {
        return ProjectDescriptorReadResult{ .succeeded = false, .descriptor = {}, .error = "Project descriptor contains trailing data." };
    }
    if (descriptor.engineAssociation.empty() || descriptor.contentRoot.empty() ||
        (legacy.present && (legacy.name.empty() || legacy.defaultScene.empty()))) {
        return ProjectDescriptorReadResult{ .succeeded = false, .descriptor = {}, .error = "Project descriptor is missing required fields." };
    }

    ProjectDescriptorReadResult metaValidation = ValidateMeta(path, descriptor);
    if (!metaValidation.succeeded) {
        return metaValidation;
    }
    return ProjectDescriptorReadResult{
        .succeeded = true,
        .descriptor = std::move(descriptor),
        .error = {},
        .legacySettings = std::move(legacy),
    };
}

} // namespace kb::project
