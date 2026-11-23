#include "CliCommands.hpp"
#include "CliProgram.hpp"
#include "engine/core/JsonValue.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/config/IniDocument.hpp"
#include "engine/project/ProjectManager.hpp"
#include "engine/project/ProjectSettings.hpp"
#include "engine/scene/SceneTagCatalog.hpp"
#include "engine/script/ScriptNameSuggestion.hpp"

#include <algorithm>
#include <cctype>
#include <array>
#include <filesystem>
#include <optional>
#include <span>
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

namespace {

// The scenes the game is built with, as the editor's Project Settings lists them: the startup scene first, then the
// others in order. A scene that is not here is not in the built game, whoever loads it.
[[nodiscard]] int RunScenesSetting(const ArgumentList& arguments, CommandIo io, const std::filesystem::path& projectRoot,
    const std::filesystem::path& settingsPath, kb::project::ProjectSettings settings) {
    using kb::core::JsonValue;
    std::string error;
    const std::vector<std::string> before = kb::project::ProjectBuildScenes(settings);
    std::vector<std::string> scenes = before;
    const auto resolve = [&](const std::string& text) { return ResolveProjectScene(projectRoot, text, error); };
    const auto fail = [&]() {
        io.err << "error: " << error << '\n';
        return 1;
    };
    if (const std::optional<std::string> order = arguments.Option("--order"); order.has_value()) {
        std::vector<std::string> listed;
        for (std::size_t start = 0U; start <= order->size();) {
            const std::size_t comma = std::min(order->find(',', start), order->size());
            if (comma > start) {
                const std::optional<std::string> scene = resolve(order->substr(start, comma - start));
                if (!scene.has_value()) return fail();
                if (std::ranges::find(listed, *scene) == listed.end()) listed.push_back(*scene);
            }
            start = comma + 1U;
        }
        if (listed.empty()) {
            io.err << "error: --order takes the scenes in order, the first one the game starts in, for example --order Main,Level2\n";
            return 1;
        }
        scenes = std::move(listed);
    }
    for (const std::string& text : arguments.Options("--add")) {
        const std::optional<std::string> scene = resolve(text);
        if (!scene.has_value()) return fail();
        if (std::ranges::find(scenes, *scene) == scenes.end()) scenes.push_back(*scene);
    }
    for (const std::string& text : arguments.Options("--remove")) {
        const std::optional<std::string> scene = resolve(text);
        if (!scene.has_value()) return fail();
        const auto found = std::ranges::find(scenes, *scene);
        if (found == scenes.end()) {
            io.err << "error: " << *scene << " is not in the build already; the build has:";
            for (const std::string& listed : scenes) io.err << ' ' << listed;
            io.err << '\n';
            return 1;
        }
        if (scenes.size() == 1U) {
            io.err << "error: the build needs at least one scene, and " << *scene << " is the only one\n";
            return 1;
        }
        if (found == scenes.begin()) {
            io.err << "error: " << *scene << " is the startup scene; make another one the startup scene first: project-settings scenes --startup <scene>\n";
            return 1;
        }
        scenes.erase(found);
    }
    if (const std::optional<std::string> startup = arguments.Option("--startup"); startup.has_value()) {
        const std::optional<std::string> scene = resolve(*startup);
        if (!scene.has_value()) return fail();
        std::erase(scenes, *scene);
        scenes.insert(scenes.begin(), *scene);
    }
    if (scenes.size() > kb::project::kProjectAdditionalScenesMax + 1U) {
        io.err << "error: a build holds at most " << kb::project::kProjectAdditionalScenesMax + 1U << " scenes\n";
        return 1;
    }
    const bool changed = scenes != before;
    if (changed) {
        kb::project::SetProjectBuildScenes(settings, scenes);
        if (!kb::project::ProjectSettingsStore::Save(settingsPath, settings, error)) {
            io.err << "error: " << error << '\n';
            return 1;
        }
    }
    const std::vector<std::string> inBuild = kb::project::ProjectBuildScenes(settings);
    const std::vector<std::string> all = ProjectScenePaths(projectRoot);
    io.out << (changed ? "scenes in the build now (" : "scenes in the build (") << inBuild.size() << "):\n";
    auto rows = JsonValue::MakeArray();
    for (std::size_t index = 0U; index < inBuild.size(); ++index) {
        const bool exists = std::ranges::find(all, inBuild[index]) != all.end();
        io.out << "  " << index + 1U << ". " << inBuild[index] << (index == 0U ? " (startup)" : "") << (exists ? "" : " (the file is missing)") << '\n';
        auto row = JsonValue::MakeObject();
        row.Set("scene", JsonValue::MakeString(ToValidUtf8(inBuild[index])));
        row.Set("startup", JsonValue::MakeBool(index == 0U));
        row.Set("exists", JsonValue::MakeBool(exists));
        rows.Append(std::move(row));
    }
    auto left = JsonValue::MakeArray();
    std::string leftText;
    for (const std::string& scene : all) {
        if (std::ranges::find(inBuild, scene) != inBuild.end()) continue;
        leftText += (leftText.empty() ? "" : " ") + scene;
        left.Append(JsonValue::MakeString(ToValidUtf8(scene)));
    }
    if (!leftText.empty()) {
        io.out << "not in the build: " << leftText << '\n';
    }
    auto warnings = JsonValue::MakeArray();
    for (const ScriptSceneReference& reference : ScenesReferencedButNotInBuild(projectRoot)) {
        io.out << "warning: " << reference.script << " opens " << reference.scene << ", which is not in the build: project-settings scenes --add " << reference.scene << '\n';
        warnings.Append(JsonValue::MakeString(ToValidUtf8(reference.scene)));
    }
    JsonValue data = DataObject("21kb.cli.project-settings-scenes/v1");
    data.Set("scenes", std::move(rows));
    data.Set("startup", JsonValue::MakeString(ToValidUtf8(inBuild.empty() ? std::string{} : inBuild.front())));
    data.Set("notInBuild", std::move(left));
    data.Set("referencedNotInBuild", std::move(warnings));
    data.Set("changed", JsonValue::MakeBool(changed));
    PublishData(io, std::move(data));
    return 0;
}

// The tags the project defines for its objects besides the built-in ones, as the editor's Project Settings > Tags lists them.
[[nodiscard]] int RunTagsSetting(const ArgumentList& arguments, CommandIo io, const std::filesystem::path& settingsPath,
    kb::project::ProjectSettings settings) {
    using kb::core::JsonValue;
    const std::vector<std::string> before = settings.tags;
    std::string error;
    for (const std::string& name : arguments.Options("--add")) {
        const std::string tag = kb::scene::SceneTagCatalog::CanonicalName(name);
        if (tag.empty()) {
            io.err << "error: '" << name << "' is not a tag name: use letters, digits and spaces, for example --add Boss\n";
            return 1;
        }
        if (kb::scene::SceneTagCatalog::IsBuiltIn(tag)) {
            io.err << "error: " << tag << " is a built-in tag; the project adds its own, for example --add Boss\n";
            return 1;
        }
        if (std::ranges::find(settings.tags, tag) != settings.tags.end()) continue;
        if (!kb::project::AddProjectTag(settings, tag)) {
            io.err << "error: the project holds at most " << kb::project::kProjectTagsMax << " tags of its own\n";
            return 1;
        }
    }
    for (const std::string& name : arguments.Options("--remove")) {
        const std::string tag = kb::scene::SceneTagCatalog::CanonicalName(name);
        if (kb::scene::SceneTagCatalog::IsBuiltIn(tag)) {
            io.err << "error: " << tag << " is a built-in tag and stays\n";
            return 1;
        }
        if (!kb::project::RemoveProjectTag(settings, tag)) {
            const std::string closest = kb::script::ClosestScriptName(tag, std::span<const std::string>{ settings.tags });
            io.err << "error: the project has no tag '" << name << '\'' << (closest.empty() ? std::string{} : "; did you mean " + closest + "?")
                   << " (its tags:";
            for (const std::string& listed : settings.tags) io.err << ' ' << listed;
            io.err << (settings.tags.empty() ? " none)\n" : ")\n");
            return 1;
        }
    }
    const bool changed = settings.tags != before;
    if (changed && !kb::project::ProjectSettingsStore::Save(settingsPath, settings, error)) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    io.out << (changed ? "tags of the project now (" : "tags of the project (") << settings.tags.size() << "):";
    auto tags = JsonValue::MakeArray();
    for (const std::string& tag : settings.tags) {
        io.out << ' ' << tag;
        tags.Append(JsonValue::MakeString(ToValidUtf8(tag)));
    }
    io.out << (settings.tags.empty() ? " none" : "") << "\nbuilt in:";
    auto builtIn = JsonValue::MakeArray();
    for (const std::string_view tag : kb::scene::SceneTagCatalog::DefaultNames) {
        io.out << ' ' << tag;
        builtIn.Append(JsonValue::MakeString(std::string{ tag }));
    }
    io.out << '\n';
    JsonValue data = DataObject("21kb.cli.project-settings-tags/v1");
    data.Set("tags", std::move(tags));
    data.Set("builtIn", std::move(builtIn));
    data.Set("changed", JsonValue::MakeBool(changed));
    PublishData(io, std::move(data));
    return 0;
}

} // namespace

// Every project setting as Section.Key=value, named as the settings file names them (Maps.DefaultMap,
// Identity.GameName, Display.WindowWidth); --set Section.Key=value changes one, checked as the editor
// checks it: an unknown name is refused, a number is brought into its range, a scene must exist.
// `scenes` lists and changes the scenes the game is built with, `tags` the project's own tags.
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
    if (!arguments.Positionals().empty()) {
        const std::string& topic = arguments.Positionals().front();
        if (topic == "scenes") return RunScenesSetting(arguments, io, projectFile.parent_path(), settingsPath, loaded.settings);
        if (topic == "tags") return RunTagsSetting(arguments, io, settingsPath, loaded.settings);
        io.err << "error: project-settings takes scenes, tags or --set Section.Key=value, not '" << topic << "'\n";
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
        // Another startup scene must not drop the old one from the game: it stays among the scenes the game is built with.
        if (!loaded.settings.defaultMap.empty() && settings.defaultMap != loaded.settings.defaultMap) {
            std::vector<std::string> scenes{ settings.defaultMap, loaded.settings.defaultMap };
            scenes.insert(scenes.end(), settings.additionalScenes.begin(), settings.additionalScenes.end());
            kb::project::SetProjectBuildScenes(settings, std::move(scenes));
        }
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
