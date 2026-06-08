#include "engine/project/ProjectDescriptorWriter.hpp"

#include "project/ProjectDescriptorFieldCodec.hpp"
#include "project/ProjectDescriptorFormat.hpp"

#include <fstream>
#include <system_error>

namespace kb::project {
namespace {

[[nodiscard]] bool PrepareOutputPath(const std::filesystem::path& path) {
    if (!path.has_parent_path()) {
        return true;
    }

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    return !error;
}

void WriteField(std::ostream& output, std::string_view key, std::string_view value) {
    output << key << '=' << ProjectDescriptorFieldCodec::Escape(value) << '\n';
}

void WriteBool(std::ostream& output, std::string_view key, bool value) {
    output << key << '=' << (value ? 1 : 0) << '\n';
}

} // namespace

bool ProjectDescriptorWriter::Write(const std::filesystem::path& path, const ProjectDescriptor& descriptor) {
    if (!PrepareOutputPath(path) || descriptor.name.empty() || descriptor.contentRoot.empty()) {
        return false;
    }

    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    if (!output.is_open()) {
        return false;
    }

    output << ProjectDescriptorFormat::Header << '\n';
    output << ProjectDescriptorFormat::FileVersionKey << '=' << ProjectDescriptor::CurrentFileVersion << '\n';
    WriteField(output, ProjectDescriptorFormat::EngineAssociationKey, descriptor.engineAssociation);
    WriteField(output, ProjectDescriptorFormat::NameKey, descriptor.name);
    WriteField(output, ProjectDescriptorFormat::CategoryKey, descriptor.category);
    WriteField(output, ProjectDescriptorFormat::DescriptionKey, descriptor.description);
    WriteField(output, ProjectDescriptorFormat::ContentRootKey, descriptor.contentRoot);
    WriteField(output, ProjectDescriptorFormat::DefaultSceneKey, descriptor.defaultScene);
    WriteBool(output, ProjectDescriptorFormat::DisableEnginePluginsByDefaultKey, descriptor.disableEnginePluginsByDefault);

    output << ProjectDescriptorFormat::TargetPlatformsCountKey << '=' << descriptor.targetPlatforms.size() << '\n';
    for (std::size_t index = 0; index < descriptor.targetPlatforms.size(); ++index) {
        WriteField(output, "targetPlatforms." + std::to_string(index), descriptor.targetPlatforms[index]);
    }

    output << ProjectDescriptorFormat::ModulesCountKey << '=' << descriptor.modules.size() << '\n';
    for (std::size_t index = 0; index < descriptor.modules.size(); ++index) {
        const std::string prefix = "modules." + std::to_string(index) + '.';
        WriteField(output, prefix + "name", descriptor.modules[index].name);
        WriteField(output, prefix + "type", descriptor.modules[index].type);
        WriteField(output, prefix + "loadingPhase", descriptor.modules[index].loadingPhase);
    }

    output << ProjectDescriptorFormat::PluginsCountKey << '=' << descriptor.plugins.size() << '\n';
    for (std::size_t index = 0; index < descriptor.plugins.size(); ++index) {
        const std::string prefix = "plugins." + std::to_string(index) + '.';
        WriteField(output, prefix + "name", descriptor.plugins[index].name);
        WriteBool(output, prefix + "enabled", descriptor.plugins[index].enabled);
    }

    return output.good();
}

} // namespace kb::project
