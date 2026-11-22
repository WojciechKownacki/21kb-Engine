#include "CliCommands.hpp"
#include "engine/core/JsonValue.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/config/IniDocument.hpp"
#include "engine/project/ProjectManager.hpp"
#include "engine/project/ProjectSettings.hpp"

#include <algorithm>
#include <cctype>
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
    using kb::core::JsonValue;
    JsonValue data = DataObject("21kb.cli.lighting-quality/v1");
    auto rows = JsonValue::MakeArray();
    for (const QualityOption& option : kQualityOptions) {
        io.out << option.label << ": " << kb::project::ProjectQualityLevelName(settings.*option.member) << '\n';
        auto row = JsonValue::MakeObject();
        row.Set("option", JsonValue::MakeString(std::string{ option.flag }));
        row.Set("label", JsonValue::MakeString(std::string{ option.label }));
        row.Set("level", JsonValue::MakeString(std::string{ kb::project::ProjectQualityLevelName(settings.*option.member) }));
        rows.Append(std::move(row));
    }
    data.Set("settings", std::move(rows));
    PublishData(io, std::move(data));
    return 0;
}

namespace {

// The words a setting that is a choice takes, for the message that refuses another.
[[nodiscard]] std::string_view SettingChoices(std::string_view key) noexcept {
    if (key == "WindowMode") return "Windowed or BorderlessFullscreen (full screen)";
    if (key.ends_with("Quality") || key.starts_with("GiQuality") || key.starts_with("ShadowQuality")) return "Auto, Low, Medium, High or Ultra";
    if (key == "LightingPath") return "Auto, Forward, Deferred or ForwardPlus";
    if (key == "HdrOutput") return "Off or Auto";
    if (key == "AndroidGraphicsApi") return "Auto, Vulkan or OpenGLES";
    if (key == "SplashScale") return "Fit, Fill or Center";
    return {};
}

} // namespace

// Every project setting as Section.Key=value, named as the settings file names them (Maps.DefaultMap,
// Identity.GameName, Display.WindowWidth); --set Section.Key=value changes one, checked as the editor
// checks it: an unknown name is refused, a number is brought into its range, a scene must exist.
int RunProjectSettingsCommand(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> project = arguments.Option("--project");
    if (!project.has_value()) {
        io.err << "error: project-settings requires --project <dir>\n";
        return 1;
    }
    std::string error;
    const std::filesystem::path projectFile = kb::project::ProjectManager::FindProjectFile(kb::assets::Utf8Path(*project), error);
    if (projectFile.empty()) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    const std::filesystem::path settingsPath = kb::project::ProjectSettingsStore::FilePath(projectFile.parent_path());
    const kb::project::ProjectSettingsLoadResult loaded = kb::project::ProjectSettingsStore::Load(settingsPath);
    if (!loaded.Succeeded()) {
        io.err << "error: " << loaded.error << '\n';
        return 1;
    }
    constexpr std::string_view kSectionPrefix = "Project.";
    kb::config::IniDocument known;
    kb::project::ProjectSettingsStore::Write(loaded.settings, known);
    kb::config::IniDocument edited = known;
    std::vector<std::pair<std::string, std::string>> changed;
    for (const std::string& assignment : arguments.Options("--set")) {
        const std::size_t equals = assignment.find('=');
        const std::string name = assignment.substr(0U, equals);
        const std::size_t dot = name.rfind('.');
        if (equals == std::string::npos || dot == std::string::npos || dot == 0U) {
            io.err << "error: --set takes Section.Key=value, for example Maps.DefaultMap=/Game/Scenes/Main.21kbscene, got '" << assignment << "'\n";
            return 1;
        }
        std::string section = name.substr(0U, dot);
        if (!section.starts_with(kSectionPrefix)) section.insert(0U, kSectionPrefix);
        const std::string key = name.substr(dot + 1U);
        const std::string value = assignment.substr(equals + 1U);
        if (!known.GetString(section, key).has_value()) {
            io.err << "error: the project has no setting '" << name << "'; kb_cli project-settings --project <dir> lists them\n";
            return 1;
        }
        // A scene setting names a scene of the project.
        if ((key == "DefaultMap" || key == "LastOpenMap" || key.starts_with("Scene")) && !value.empty()) {
            const bool virtualPath = value.starts_with("/Game/") && value.ends_with(".21kbscene");
            std::error_code missing;
            if (!virtualPath || !std::filesystem::is_regular_file(projectFile.parent_path() / "Assets" / kb::assets::Utf8Path(value.substr(6U)), missing)) {
                io.err << "error: " << name << " takes a scene of the project as /Game/<folder>/<Name>.21kbscene, and '" << value << "' is not one\n";
                return 1;
            }
        }
        edited.SetString(section, key, value);
        changed.emplace_back(section, key);
    }
    kb::project::ProjectSettings settings = loaded.settings;
    if (!changed.empty()) {
        settings = kb::project::ProjectSettingsStore::Read(edited);
        // A value the setting does not take is refused rather than quietly replaced by its default; a number
        // only brought into its range is kept as brought in.
        kb::config::IniDocument taken;
        kb::project::ProjectSettingsStore::Write(settings, taken);
        for (const auto& [section, key] : changed) {
            const std::string wanted{ edited.GetString(section, key).value_or("") };
            const std::string kept{ taken.GetString(section, key).value_or("") };
            const auto lower = [](std::string text) {
                std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return text;
            };
            const bool number = !wanted.empty() && wanted.find_first_not_of("+-0123456789.eE") == std::string::npos;
            if (lower(wanted) == lower(kept) || number) continue;
            const std::string name = section.substr(kSectionPrefix.size()) + "." + key;
            io.err << "error: " << name << " does not take '" << wanted << "' (it would stay '" << kept << "')";
            if (const std::string_view choices = SettingChoices(key); !choices.empty()) io.err << "; it takes " << choices;
            io.err << '\n';
            return 1;
        }
        if (!kb::project::ProjectSettingsStore::Save(settingsPath, settings, error)) {
            io.err << "error: " << error << '\n';
            return 1;
        }
    }
    // What the file now says, as Load reads it: every setting, or the changed ones with the value they took.
    kb::config::IniDocument now;
    kb::project::ProjectSettingsStore::Write(settings, now);
    using kb::core::JsonValue;
    JsonValue data = DataObject("21kb.cli.project-settings/v1");
    JsonValue values = JsonValue::MakeObject();
    for (const auto& [section, entries] : now.Sections()) {
        const std::string shown = section.starts_with(kSectionPrefix) ? section.substr(kSectionPrefix.size()) : section;
        for (const auto& [key, value] : entries) {
            const bool wanted = changed.empty() || std::ranges::find(changed, std::pair<std::string, std::string>{ section, key }) != changed.end();
            if (!wanted) continue;
            io.out << shown << '.' << key << '=' << value << '\n';
            values.Set(shown + "." + key, JsonValue::MakeString(value));
        }
    }
    data.Set("settings", std::move(values));
    data.Set("changed", JsonValue::MakeBool(!changed.empty()));
    PublishData(io, std::move(data));
    return 0;
}

} // namespace kb::cli
