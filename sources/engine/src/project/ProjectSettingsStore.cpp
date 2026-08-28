#include "engine/project/ProjectSettings.hpp"

#include "engine/config/IniDocument.hpp"

#include <string_view>
#include <system_error>

namespace kb::project {
namespace {

constexpr std::string_view kIdentity = "Project.Identity";
constexpr std::string_view kMaps = "Project.Maps";
constexpr std::string_view kRendering = "Project.Rendering";
constexpr std::string_view kInput = "Project.Input";
constexpr std::string_view kPhysics = "Project.Physics";

[[nodiscard]] std::string_view LightingPathName(ProjectSceneLightingPath path) noexcept {
    switch (path) {
    case ProjectSceneLightingPath::Deferred: return "Deferred";
    case ProjectSceneLightingPath::ForwardPlus: return "ForwardPlus";
    case ProjectSceneLightingPath::Forward:
    default: return "Forward";
    }
}

[[nodiscard]] ProjectSceneLightingPath ParseLightingPath(std::string_view name, ProjectSceneLightingPath fallback) noexcept {
    if (name == "Forward") return ProjectSceneLightingPath::Forward;
    if (name == "Deferred") return ProjectSceneLightingPath::Deferred;
    if (name == "ForwardPlus") return ProjectSceneLightingPath::ForwardPlus;
    return fallback;
}

[[nodiscard]] std::string ReadString(const config::IniDocument& document, std::string_view section, std::string_view key, std::string fallback) {
    const std::optional<std::string_view> value = document.GetString(section, key);
    return value.has_value() ? std::string{*value} : std::move(fallback);
}

} // namespace

std::filesystem::path ProjectSettingsStore::FilePath(const std::filesystem::path& projectRoot) {
    return projectRoot / "Config" / "ProjectSettings.ini";
}

ProjectSettings ProjectSettingsStore::FromDescriptor(const ProjectDescriptor& descriptor) {
    ProjectSettings settings;
    settings.name = descriptor.name;
    settings.gameName = descriptor.name;
    settings.category = descriptor.category;
    settings.description = descriptor.description;
    settings.defaultMap = descriptor.defaultScene;
    settings.lightingPath = descriptor.sceneLightingPath;
    settings.inputEnabled = descriptor.inputEnabled;
    settings.inputMappingContext = descriptor.inputMappingContext;
    settings.physicsLayersAsset = descriptor.physicsLayersAsset;
    return settings;
}

ProjectSettingsLoadResult ProjectSettingsStore::Load(const std::filesystem::path& path) {
    ProjectSettingsLoadResult result;

    std::error_code filesystemError;
    if (!std::filesystem::exists(path, filesystemError) || filesystemError) {
        return result;
    }

    config::IniDocument document;
    if (!document.Load(path, result.error)) {
        return result;
    }
    result.found = true;

    const ProjectSettings defaults;
    result.settings.name = ReadString(document, kIdentity, "Name", defaults.name);
    result.settings.gameName = ReadString(document, kIdentity, "GameName", defaults.gameName);
    result.settings.category = ReadString(document, kIdentity, "Category", defaults.category);
    result.settings.description = ReadString(document, kIdentity, "Description", defaults.description);
    result.settings.defaultMap = ReadString(document, kMaps, "DefaultMap", defaults.defaultMap);
    result.settings.lastOpenMap = ReadString(document, kMaps, "LastOpenMap", defaults.lastOpenMap);
    result.settings.lightingPath = ParseLightingPath(
        ReadString(document, kRendering, "LightingPath", std::string{LightingPathName(defaults.lightingPath)}),
        defaults.lightingPath);
    result.settings.inputEnabled = document.GetBool(kInput, "Enabled").value_or(defaults.inputEnabled);
    result.settings.inputMappingContext = ReadString(document, kInput, "MappingContext", defaults.inputMappingContext);
    result.settings.physicsLayersAsset = ReadString(document, kPhysics, "LayersAsset", defaults.physicsLayersAsset);
    return result;
}

bool ProjectSettingsStore::Save(
    const std::filesystem::path& path,
    const ProjectSettings& settings,
    std::string& error) {
    error.clear();

    // Read first, so anything a person added to the file by hand - a section this
    // build does not know, a comment's neighbouring key - survives the write.
    config::IniDocument document;
    std::error_code filesystemError;
    if (std::filesystem::exists(path, filesystemError) && !filesystemError) {
        std::string loadError;
        if (!document.Load(path, loadError)) {
            document.Clear();
        }
    }

    document.SetString(kIdentity, "Name", settings.name);
    document.SetString(kIdentity, "GameName", settings.gameName);
    document.SetString(kIdentity, "Category", settings.category);
    document.SetString(kIdentity, "Description", settings.description);
    document.SetString(kMaps, "DefaultMap", settings.defaultMap);
    document.SetString(kMaps, "LastOpenMap", settings.lastOpenMap);
    document.SetString(kRendering, "LightingPath", std::string{LightingPathName(settings.lightingPath)});
    document.SetBool(kInput, "Enabled", settings.inputEnabled);
    document.SetString(kInput, "MappingContext", settings.inputMappingContext);
    document.SetString(kPhysics, "LayersAsset", settings.physicsLayersAsset);
    return document.Save(path, error);
}

} // namespace kb::project
