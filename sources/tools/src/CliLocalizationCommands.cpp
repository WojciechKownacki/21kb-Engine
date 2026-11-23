#include "CliAssetTarget.hpp"
#include "CliCommands.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/localization/LocalizationCatalogIO.hpp"
#include "engine/localization/LocalizationLocale.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/ui/UIComponentPropertyCatalog.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

// localization: the texts of a game in several languages, as the engine's .kbloc catalog the Text component reads
// through its localizationKey. One catalog per project (Assets/Localization/Game.kbloc); its default language is the
// one the game starts in and the one a missing text falls back to. `collect` turns the texts that are written on
// the scene's controls into keys, so every one of them can be translated.

namespace kb::cli {
namespace {

using kb::localization::LocalizationCatalog;
using kb::localization::LocalizationMessage;

constexpr std::string_view kUsage =
    "usage: kb_cli localization <collect|set|add-language|show|missing|default> --project <dir> ...\n"
    "  collect      [--language en] [--scene <path>]      (the texts written on the scene's controls become keys of the default language)\n"
    "  set          --language de --set key=text [--set key=text ...]\n"
    "  add-language --language de                         (starts it with the default language's texts, to be translated)\n"
    "  show | missing | default --language <code>\n"
    "  every command takes --catalog <path> for another catalog than the project's only one (Assets/Localization/Game.kbloc)\n";

[[nodiscard]] bool ValidKey(std::string_view key) {
    return !key.empty() && key.size() <= 128U && std::ranges::all_of(key, [](unsigned char c) { return std::isalnum(c) || c == '.' || c == '_' || c == '-'; });
}

struct OpenCatalog {
    std::filesystem::path file;
    std::string virtualPath;
    LocalizationCatalog catalog;
    bool isNew = false;
};

// The catalog: --catalog, else the only one of the project, else (when `create`) a new Game.kbloc with `language` as its default.
[[nodiscard]] std::optional<OpenCatalog> OpenOrCreate(const ArgumentList& arguments, bool create, const std::string& language, CommandIo io) {
    const std::optional<std::string> project = arguments.Option("--project");
    if (!project.has_value()) {
        io.err << "error: localization requires --project <dir>\n" << kUsage;
        return std::nullopt;
    }
    kb::scene::Scene scene;
    std::string error;
    if (!MountProjectAssets(scene, kb::assets::Utf8Path(*project), error)) {
        io.err << "error: " << error << '\n';
        return std::nullopt;
    }
    const kb::assets::AssetMetadata* found = nullptr;
    if (const std::optional<std::string> path = arguments.Option("--catalog")) {
        found = FindAssetByFlexiblePath(scene.Assets().Manager().Registry(), kb::assets::Utf8Path(*path));
        if (found == nullptr || found->type != kb::localization::kLocalizationCatalogAssetType) {
            io.err << "error: --catalog <path> must name a localization catalog of the project (/Game/Localization/Game.kbloc)\n";
            return std::nullopt;
        }
    } else {
        for (const kb::assets::AssetMetadata& metadata : scene.Assets().Manager().Registry().All()) {
            if (metadata.type == kb::localization::kLocalizationCatalogAssetType && metadata.virtualPath.generic_string().starts_with("/Game/")) {
                found = &metadata;
                break;
            }
        }
    }
    OpenCatalog open;
    if (found != nullptr) {
        std::optional<LocalizationCatalog> loaded = kb::localization::LocalizationCatalogIO::Load(found->physicalPath);
        if (!loaded.has_value()) {
            io.err << "error: " << found->virtualPath.generic_string() << " could not be read\n";
            return std::nullopt;
        }
        open.file = found->physicalPath;
        open.virtualPath = found->virtualPath.generic_string();
        open.catalog = std::move(*loaded);
        return open;
    }
    if (!create) {
        io.err << "error: the project has no localization catalog yet (kb_cli localization collect or localization set --language en --set key=text makes it)\n";
        return std::nullopt;
    }
    const std::optional<AssetTarget> target = ResolveAssetTarget(arguments.WithOption("--name", "Game"), "localization", "--project <dir>", "Assets/Localization",
        kb::localization::kLocalizationCatalogAssetExtension, true, io);
    if (!target.has_value()) return std::nullopt;
    open.file = target->file;
    open.virtualPath = target->virtualPath;
    open.catalog.fallbackLanguage = language;
    open.isNew = true;
    return open;
}

[[nodiscard]] bool Save(const OpenCatalog& open, CommandIo io) {
    std::error_code error;
    std::filesystem::create_directories(open.file.parent_path(), error);
    if (!kb::localization::LocalizationCatalogIO::Save(open.file, open.catalog)) {
        io.err << "error: " << open.virtualPath << " could not be saved (a language needs at least one text; a text is not empty)\n";
        return false;
    }
    return true;
}

[[nodiscard]] std::optional<std::string> LanguageOption(const ArgumentList& arguments, bool required, CommandIo io) {
    const std::optional<std::string> language = arguments.Option("--language");
    if (!language.has_value()) {
        if (required) io.err << "error: --language <code> is required, a code such as en, de, pl or pt-BR\n";
        return std::nullopt;
    }
    if (!kb::localization::LocalizationLocaleKey(*language).has_value()) {
        io.err << "error: --language takes a code such as en, de, pl or pt-BR, got '" << *language << "'\n";
        return std::nullopt;
    }
    return language;
}

// The language as the catalog spells it (de and DE are one language).
[[nodiscard]] std::string CatalogSpelling(const LocalizationCatalog& catalog, const std::string& language) {
    const auto wanted = kb::localization::LocalizationLocaleKey(language);
    for (const auto& [tag, entries] : catalog.languages) {
        if (kb::localization::LocalizationLocaleKey(tag) == wanted) return tag;
    }
    return language;
}

[[nodiscard]] std::string Quoted(const std::string& text) { return "\"" + text + "\""; }

[[nodiscard]] std::string ShowCatalog(const OpenCatalog& open) {
    std::string text = "catalog " + open.virtualPath + ", default language " + open.catalog.fallbackLanguage + "\n  languages:";
    for (const auto& [language, entries] : open.catalog.languages) text += " " + language + " (" + std::to_string(entries.size()) + " texts)";
    text += "\n";
    std::set<std::string> keys;
    for (const auto& [language, entries] : open.catalog.languages) {
        for (const auto& [key, message] : entries) keys.insert(key);
    }
    for (const std::string& key : keys) {
        text += "  " + key + ":";
        for (const auto& [language, entries] : open.catalog.languages) {
            const auto found = entries.find(key);
            text += " " + language + "=" + (found == entries.end() ? std::string{ "(missing)" } : Quoted(found->second.text));
        }
        text += "\n";
    }
    return text;
}

[[nodiscard]] int Set(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> language = LanguageOption(arguments, true, io);
    const std::vector<std::string> settings = arguments.Options("--set");
    if (!language.has_value()) return 1;
    if (settings.empty()) {
        io.err << "error: localization set needs --set key=text (for example --set menu.play=\"Play\")\n";
        return 1;
    }
    std::optional<OpenCatalog> open = OpenOrCreate(arguments, true, *language, io);
    if (!open.has_value()) return 1;
    const std::string spelled = CatalogSpelling(open->catalog, *language);
    auto& entries = open->catalog.languages[spelled];
    for (const std::string& setting : settings) {
        const std::size_t equals = setting.find('=');
        const std::string key = setting.substr(0U, equals);
        const std::string text = equals == std::string::npos ? std::string{} : setting.substr(equals + 1U);
        if (!ValidKey(key) || text.empty()) {
            io.err << "error: --set " << setting << ": expects key=text, a key of letters, digits, . _ - and a text that is not empty\n";
            return 1;
        }
        entries[key] = LocalizationMessage{ text, {} };
    }
    if (!Save(*open, io)) return 1;
    io.out << (open->isNew ? "created " : "updated ") << open->virtualPath << ": " << settings.size() << " texts in " << spelled << " (default language " << open->catalog.fallbackLanguage << ")\n"
           << "next: kb_cli localization missing (what still has no translation)\n";
    return 0;
}

[[nodiscard]] int AddLanguage(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> language = LanguageOption(arguments, true, io);
    if (!language.has_value()) return 1;
    std::optional<OpenCatalog> open = OpenOrCreate(arguments, false, *language, io);
    if (!open.has_value()) return 1;
    const std::string spelled = CatalogSpelling(open->catalog, *language);
    if (open->catalog.languages.contains(spelled)) {
        io.err << "error: " << spelled << " is in the catalog already (kb_cli localization missing shows what is not translated)\n";
        return 1;
    }
    const auto defaults = open->catalog.languages.find(CatalogSpelling(open->catalog, open->catalog.fallbackLanguage));
    if (defaults == open->catalog.languages.end() || defaults->second.empty()) {
        io.err << "error: the default language has no texts to start from (kb_cli localization collect, or set a text)\n";
        return 1;
    }
    open->catalog.languages[spelled] = defaults->second;
    if (!Save(*open, io)) return 1;
    io.out << "added " << spelled << " to " << open->virtualPath << " with the " << defaults->second.size() << " texts of " << open->catalog.fallbackLanguage
           << " to translate: kb_cli localization set --language " << spelled << " --set key=text ...\n";
    return 0;
}

[[nodiscard]] int Show(const ArgumentList& arguments, CommandIo io) {
    const std::optional<OpenCatalog> open = OpenOrCreate(arguments, false, {}, io);
    if (!open.has_value()) return 1;
    io.out << ShowCatalog(*open);
    return 0;
}

[[nodiscard]] int Missing(const ArgumentList& arguments, CommandIo io) {
    const std::optional<OpenCatalog> open = OpenOrCreate(arguments, false, {}, io);
    if (!open.has_value()) return 1;
    const LocalizationCatalog& catalog = open->catalog;
    const std::string defaultLanguage = CatalogSpelling(catalog, catalog.fallbackLanguage);
    const auto defaults = catalog.languages.find(defaultLanguage);
    std::size_t problems = 0U;
    for (const auto& [language, entries] : catalog.languages) {
        if (language == defaultLanguage || defaults == catalog.languages.end()) continue;
        for (const auto& [key, message] : defaults->second) {
            const auto found = entries.find(key);
            if (found == entries.end()) {
                io.out << language << ": " << key << " has no text (default: " << Quoted(message.text) << ")\n";
                ++problems;
            } else if (found->second.text == message.text) {
                io.out << language << ": " << key << " is still the " << defaultLanguage << " text " << Quoted(message.text) << "\n";
                ++problems;
            }
        }
    }
    if (problems == 0U) io.out << "every text is translated in every language of " << open->virtualPath << "\n";
    else io.out << problems << " to translate: kb_cli localization set --language <code> --set key=text ...\n";
    return 0;
}

[[nodiscard]] int Default(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> language = LanguageOption(arguments, true, io);
    if (!language.has_value()) return 1;
    std::optional<OpenCatalog> open = OpenOrCreate(arguments, false, {}, io);
    if (!open.has_value()) return 1;
    const std::string spelled = CatalogSpelling(open->catalog, *language);
    if (!open->catalog.languages.contains(spelled)) {
        io.err << "error: the catalog has no language " << *language << " (kb_cli localization add-language --language " << *language << " first)\n";
        return 1;
    }
    open->catalog.fallbackLanguage = spelled;
    if (!Save(*open, io)) return 1;
    io.out << "the default language of " << open->virtualPath << " is now " << spelled << ": the game starts in it, and a missing text falls back to it\n";
    return 0;
}

[[nodiscard]] std::string KeyFor(std::string_view objectName, const std::set<std::string>& taken) {
    std::string key = "ui.";
    for (const unsigned char c : objectName) key += std::isalnum(c) ? static_cast<char>(std::tolower(c)) : '_';
    if (key.size() == 3U) key += "text";
    std::string unique = key;
    for (int number = 2; taken.contains(unique); ++number) unique = key + "_" + std::to_string(number);
    return unique;
}

// The script that gives the game its catalog when the scene starts; it starts in the catalog's default language.
[[nodiscard]] std::string StartScript(const std::string& catalogPath, const std::string& catalogId) {
    return "-- @asset " + catalogPath + "\nfunction Ready(self, dt)\n    Localization.SetCatalog(\"" + catalogId + "\")\nend\n";
}

// The catalog's asset id as the script function takes it: a hexadecimal string.
[[nodiscard]] std::optional<std::string> CatalogId(const ArgumentList& arguments, const std::string& catalogPath) {
    kb::scene::Scene scene;
    std::string error;
    if (!MountProjectAssets(scene, kb::assets::Utf8Path(arguments.Option("--project").value_or(std::string{})), error)) return std::nullopt;
    const kb::assets::AssetMetadata* metadata = FindAssetByFlexiblePath(scene.Assets().Manager().Registry(), kb::assets::Utf8Path(catalogPath));
    if (metadata == nullptr) return std::nullopt;
    return kb::assets::ToString(metadata->id);
}

[[nodiscard]] int Collect(const ArgumentList& arguments, CommandIo io) {
    const std::string language = LanguageOption(arguments, false, io).value_or(arguments.Option("--language").has_value() ? std::string{} : std::string{ "en" });
    if (language.empty()) return 1;
    std::string error;
    const std::optional<ProjectSceneDocument> scene = LoadProjectScene(arguments, error);
    if (!scene.has_value()) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    std::optional<OpenCatalog> open = OpenOrCreate(arguments, true, language, io);
    if (!open.has_value()) return 1;
    const std::string spelled = CatalogSpelling(open->catalog, open->isNew ? language : open->catalog.fallbackLanguage);
    auto& entries = open->catalog.languages[spelled];
    std::set<std::string> taken;
    for (const auto& [lang, texts] : open->catalog.languages) {
        for (const auto& [key, message] : texts) taken.insert(key);
    }
    struct Found {
        std::string path;
        std::string key;
        std::string text;
    };
    std::vector<Found> found;
    const std::span<const kb::scene::ScenePrefabNodeDesc> nodes = scene->document.worldPrefab.Nodes();
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        kb::scene::UIComponentPropertyValue content;
        kb::scene::UIComponentPropertyValue key;
        const auto& ui = nodes[index].components.ui;
        if (!kb::scene::ReadUIComponentProperty(ui, kb::scene::UIComponentType::Text, "content", content) ||
            !kb::scene::ReadUIComponentProperty(ui, kb::scene::UIComponentType::Text, "localizationKey", key)) continue;
        const std::string* text = std::get_if<std::string>(&content);
        const std::string* current = std::get_if<std::string>(&key);
        if (text == nullptr || text->empty() || (current != nullptr && !current->empty())) continue;
        const std::string path = NodePath(nodes, index);
        const std::string name = nodes[index].name == "Text" && nodes[index].parentNode < nodes.size() ? nodes[nodes[index].parentNode].name : nodes[index].name;
        found.push_back({ path, KeyFor(name, taken), *text });
        taken.insert(found.back().key);
    }
    if (found.empty()) {
        io.err << "error: no text on the controls of " << scene->virtualPath << " without a key (add a Text or a Button first, or write the texts with localization set)\n";
        return 1;
    }
    for (const Found& item : found) entries[item.key] = LocalizationMessage{ item.text, {} };
    if (!Save(*open, io)) return 1;
    // Each control points at its key; the game gets its catalog when the scene starts.
    for (const Found& item : found) {
        std::string message;
        const std::vector<std::string> words{ "set", "--project", *arguments.Option("--project"), "--scene", scene->virtualPath, "--entity", item.path, "--type", "Text",
            "--set", "localizationKey=" + item.key };
        if (!RunSiblingCommand(&RunComponentCommand, words, message)) {
            io.err << "error: " << item.path << ": " << message;
            return 1;
        }
    }
    const std::optional<std::string> catalogId = CatalogId(arguments, open->virtualPath);
    if (!catalogId.has_value()) {
        io.err << "error: the catalog " << open->virtualPath << " was saved but is not found as an asset\n";
        return 1;
    }
    if (!AttachScriptOnObject(arguments.WithOption("--scene", scene->virtualPath), "Localization", "LocalizationSetup", StartScript(open->virtualPath, *catalogId), error)) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    io.out << (open->isNew ? "created " : "updated ") << open->virtualPath << " with " << found.size() << " texts in " << spelled << " (the default language):\n";
    for (const Found& item : found) io.out << "  " << item.key << " = " << Quoted(item.text) << "  (" << item.path << ")\n";
    io.out << "the controls use these keys, and the object Localization gives the game the catalog on start\n"
           << "next: kb_cli localization set --language <code> --set key=text ... for each other language\n";
    return 0;
}

} // namespace

int RunLocalizationCommand(const ArgumentList& arguments, CommandIo io) {
    const std::string action = arguments.Positionals().size() == 1U ? arguments.Positionals().front() : std::string{};
    if (action == "collect") return Collect(arguments, io);
    if (action == "set") return Set(arguments, io);
    if (action == "add-language") return AddLanguage(arguments, io);
    if (action == "show") return Show(arguments, io);
    if (action == "missing") return Missing(arguments, io);
    if (action == "default") return Default(arguments, io);
    io.err << kUsage;
    return 1;
}

} // namespace kb::cli
