#include "engine/project/ProjectDescriptorReader.hpp"

#include "project/ProjectDescriptorFieldCodec.hpp"
#include "project/ProjectDescriptorFormat.hpp"

#include <charconv>
#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>

namespace kb::project {
namespace {

using FieldMap = std::unordered_map<std::string, std::string>;

[[nodiscard]] bool ReadLine(std::istream& input, std::string& line) {
    if (!std::getline(input, line)) {
        return false;
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return true;
}

[[nodiscard]] bool SplitKeyValue(std::string_view line, std::string& key, std::string& value) {
    const std::size_t separator = line.find('=');
    if (separator == std::string_view::npos) {
        return false;
    }
    key.assign(line.substr(0, separator));
    value.assign(line.substr(separator + 1));
    return !key.empty();
}

[[nodiscard]] bool ParseUnsigned(std::string_view text, std::size_t& output) {
    const char* first = text.data();
    const char* last = text.data() + text.size();
    const std::from_chars_result result = std::from_chars(first, last, output);
    return result.ec == std::errc{} && result.ptr == last;
}

[[nodiscard]] bool ReadFields(std::istream& input, FieldMap& fields) {
    std::string line;
    while (ReadLine(input, line)) {
        std::string key;
        std::string value;
        if (!SplitKeyValue(line, key, value) || fields.contains(key)) {
            return false;
        }
        fields.emplace(std::move(key), std::move(value));
    }
    return true;
}

[[nodiscard]] bool ReadEscapedField(const FieldMap& fields, std::string_view key, std::string& output) {
    const auto item = fields.find(std::string{ key });
    if (item == fields.end()) {
        return false;
    }
    std::optional<std::string> value = ProjectDescriptorFieldCodec::Unescape(item->second);
    if (!value.has_value()) {
        return false;
    }
    output = std::move(*value);
    return true;
}

[[nodiscard]] bool ReadOptionalEscapedField(const FieldMap& fields, std::string_view key, std::string& output) {
    const auto item = fields.find(std::string{ key });
    if (item == fields.end()) {
        return true;
    }
    std::optional<std::string> value = ProjectDescriptorFieldCodec::Unescape(item->second);
    if (!value.has_value()) {
        return false;
    }
    output = std::move(*value);
    return true;
}

[[nodiscard]] bool ReadSizeField(const FieldMap& fields, std::string_view key, std::size_t& output) {
    const auto item = fields.find(std::string{ key });
    return item != fields.end() && ParseUnsigned(item->second, output);
}

[[nodiscard]] bool ReadBoolField(const FieldMap& fields, std::string_view key, bool& output) {
    const auto item = fields.find(std::string{ key });
    if (item == fields.end()) {
        return false;
    }
    if (item->second == "1") {
        output = true;
        return true;
    }
    if (item->second == "0") {
        output = false;
        return true;
    }
    return false;
}

[[nodiscard]] bool ReadTargetPlatforms(const FieldMap& fields, ProjectDescriptor& descriptor) {
    std::size_t count = 0;
    if (!ReadSizeField(fields, ProjectDescriptorFormat::TargetPlatformsCountKey, count)) {
        return false;
    }
    descriptor.targetPlatforms.clear();
    descriptor.targetPlatforms.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        std::string platform;
        if (!ReadEscapedField(fields, "targetPlatforms." + std::to_string(index), platform)) {
            return false;
        }
        descriptor.targetPlatforms.push_back(std::move(platform));
    }
    return true;
}

[[nodiscard]] bool ReadModules(const FieldMap& fields, ProjectDescriptor& descriptor) {
    std::size_t count = 0;
    if (!ReadSizeField(fields, ProjectDescriptorFormat::ModulesCountKey, count)) {
        return false;
    }
    descriptor.modules.clear();
    descriptor.modules.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const std::string prefix = "modules." + std::to_string(index) + '.';
        ProjectModuleDescriptor module;
        if (!ReadEscapedField(fields, prefix + "name", module.name) ||
            !ReadEscapedField(fields, prefix + "type", module.type) ||
            !ReadEscapedField(fields, prefix + "loadingPhase", module.loadingPhase)) {
            return false;
        }
        descriptor.modules.push_back(std::move(module));
    }
    return true;
}

[[nodiscard]] bool ReadPlugins(const FieldMap& fields, ProjectDescriptor& descriptor) {
    std::size_t count = 0;
    if (!ReadSizeField(fields, ProjectDescriptorFormat::PluginsCountKey, count)) {
        return false;
    }
    descriptor.plugins.clear();
    descriptor.plugins.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const std::string prefix = "plugins." + std::to_string(index) + '.';
        ProjectPluginReference plugin;
        if (!ReadEscapedField(fields, prefix + "name", plugin.name) ||
            !ReadBoolField(fields, prefix + "enabled", plugin.enabled)) {
            return false;
        }
        descriptor.plugins.push_back(std::move(plugin));
    }
    return true;
}

} // namespace

ProjectDescriptorReadResult ProjectDescriptorReader::Read(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    if (!input.is_open()) {
        return ProjectDescriptorReadResult{ .succeeded = false, .descriptor = {}, .error = "Project descriptor could not be opened." };
    }

    std::string line;
    if (!ReadLine(input, line) || line != ProjectDescriptorFormat::Header) {
        return ProjectDescriptorReadResult{ .succeeded = false, .descriptor = {}, .error = "Project descriptor header is invalid." };
    }

    FieldMap fields;
    if (!ReadFields(input, fields)) {
        return ProjectDescriptorReadResult{ .succeeded = false, .descriptor = {}, .error = "Project descriptor contains invalid fields." };
    }

    ProjectDescriptor descriptor;
    std::size_t fileVersion = 0;
    if (!ReadSizeField(fields, ProjectDescriptorFormat::FileVersionKey, fileVersion) || fileVersion == 0 || fileVersion > ProjectDescriptor::CurrentFileVersion) {
        return ProjectDescriptorReadResult{ .succeeded = false, .descriptor = {}, .error = "Project descriptor version is not supported." };
    }
    descriptor.fileVersion = static_cast<std::uint32_t>(fileVersion);

    if (!ReadEscapedField(fields, ProjectDescriptorFormat::EngineAssociationKey, descriptor.engineAssociation) ||
        !ReadEscapedField(fields, ProjectDescriptorFormat::NameKey, descriptor.name) ||
        !ReadOptionalEscapedField(fields, ProjectDescriptorFormat::CategoryKey, descriptor.category) ||
        !ReadOptionalEscapedField(fields, ProjectDescriptorFormat::DescriptionKey, descriptor.description) ||
        !ReadEscapedField(fields, ProjectDescriptorFormat::ContentRootKey, descriptor.contentRoot) ||
        !ReadEscapedField(fields, ProjectDescriptorFormat::DefaultSceneKey, descriptor.defaultScene) ||
        !ReadBoolField(fields, ProjectDescriptorFormat::DisableEnginePluginsByDefaultKey, descriptor.disableEnginePluginsByDefault) ||
        !ReadTargetPlatforms(fields, descriptor) ||
        !ReadModules(fields, descriptor) ||
        !ReadPlugins(fields, descriptor)) {
        return ProjectDescriptorReadResult{ .succeeded = false, .descriptor = {}, .error = "Project descriptor is missing required fields." };
    }

    if (descriptor.name.empty() || descriptor.contentRoot.empty()) {
        return ProjectDescriptorReadResult{ .succeeded = false, .descriptor = {}, .error = "Project descriptor contains an empty project name or content root." };
    }

    return ProjectDescriptorReadResult{ .succeeded = true, .descriptor = std::move(descriptor), .error = {} };
}

} // namespace kb::project
