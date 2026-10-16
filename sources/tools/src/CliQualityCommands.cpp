#include "CliCommands.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/project/ProjectManager.hpp"
#include "engine/project/ProjectSettings.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace kb::cli {

namespace {

struct QualityOption {
    std::string_view flag;
    std::string_view label;
    kb::project::ProjectQualityLevel kb::project::ProjectSettings::* member;
};

constexpr std::array<QualityOption, 4U> kQualityOptions{ {
    { "--shadows", "Shadows (desktop)", &kb::project::ProjectSettings::shadowQuality },
    { "--shadows-mobile", "Shadows (mobile)", &kb::project::ProjectSettings::shadowQualityMobile },
    { "--gi", "Global illumination (desktop)", &kb::project::ProjectSettings::giQuality },
    { "--gi-mobile", "Global illumination (mobile)", &kb::project::ProjectSettings::giQualityMobile },
} };

} // namespace

// The shadow and global illumination quality the project pins for computers and for phones and tablets,
// kept in the settings file's [Rendering] section. Without options the command only reports them.
int RunLightingQualityCommand(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> project = arguments.Option("--project");
    if (!project.has_value()) {
        io.err << "error: lighting-quality requires --project <dir>\n";
        return 1;
    }
    std::string error;
    const std::filesystem::path projectFile = kb::project::ProjectManager::FindProjectFile(kb::assets::Utf8Path(*project), error);
    if (projectFile.empty()) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    const std::filesystem::path settingsPath = kb::project::ProjectSettingsStore::FilePath(projectFile.parent_path());
    kb::project::ProjectSettingsLoadResult loaded = kb::project::ProjectSettingsStore::Load(settingsPath);
    if (!loaded.Succeeded()) {
        io.err << "error: " << loaded.error << '\n';
        return 1;
    }
    kb::project::ProjectSettings& settings = loaded.settings;

    bool changed = false;
    for (const QualityOption& option : kQualityOptions) {
        const std::optional<std::string> value = arguments.Option(option.flag);
        if (!value.has_value()) {
            continue;
        }
        const std::optional<kb::project::ProjectQualityLevel> level = kb::project::ParseProjectQualityLevel(*value);
        if (!level.has_value()) {
            io.err << "error: " << option.flag << " takes auto, low, medium, high or ultra, got '" << *value << "'\n";
            return 1;
        }
        changed = changed || settings.*option.member != *level;
        settings.*option.member = *level;
    }
    if (changed && !kb::project::ProjectSettingsStore::Save(settingsPath, settings, error)) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    for (const QualityOption& option : kQualityOptions) {
        io.out << option.label << ": " << kb::project::ProjectQualityLevelName(settings.*option.member) << '\n';
    }
    return 0;
}

} // namespace kb::cli
