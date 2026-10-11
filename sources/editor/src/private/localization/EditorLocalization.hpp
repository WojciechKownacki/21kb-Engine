#pragma once

#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace kb::editor {

struct EditorLanguage {
    std::string_view code;        // the catalog's file name: Content/EditorShell/Localization/<code>.kbloc
    std::string_view nativeName;  // what the Language list shows: "Polski", "Deutsch", "日本語"
    std::string_view englishName;
};

// The editor's own text in the user's language, like Unreal's LOCTEXT: every line of the interface
// names a key and its English text - `EditorText("files.rename", "Rename")` - and the catalog of the
// chosen language gives the translation. English is the source and needs no catalog; a key a
// catalog does not have shows its English text. Catalogs are the engine's .kbloc files
// (kb::localization), one per language.
class EditorLocalization {
public:
    EditorLocalization() = delete;

    // The 33 languages of the Language list, English first.
    [[nodiscard]] static std::span<const EditorLanguage> Languages() noexcept;
    [[nodiscard]] static const EditorLanguage* Find(std::string_view code) noexcept;
    // The operating system's interface language when it is one of Languages(), else English.
    [[nodiscard]] static std::string_view SystemLanguage();

    // The user's choice from Editor Settings, kept for every project like Unity's and Unreal's editor
    // language: %LOCALAPPDATA%/21kb/EditorPreferences.ini (Linux: $XDG_CONFIG_HOME/21kb). Before a
    // choice is made: SystemLanguage().
    [[nodiscard]] static std::string PreferredLanguage();
    [[nodiscard]] static bool StorePreferredLanguage(std::string_view code, std::string& error);
    [[nodiscard]] static std::filesystem::path PreferencesFile();

    [[nodiscard]] static std::string_view Current() noexcept;
    // Loads the language's catalog from `catalogFolder` and makes it current. On failure the
    // current language stays and `error` says why (missing or damaged catalog).
    [[nodiscard]] static bool SetLanguage(std::string_view code, const std::filesystem::path& catalogFolder, std::string& error);
    // Next to kb_editor.exe: Content/EditorShell/Localization.
    [[nodiscard]] static std::filesystem::path DefaultCatalogFolder();

    [[nodiscard]] static std::string_view Text(std::string_view key, std::string_view english);
    // `english` with {name} placeholders filled in, e.g. Format("files.moved", "Moved {count} items",
    // {{"count", "3"}}); a translation keeps the same placeholders in its own order.
    [[nodiscard]] static std::string Format(std::string_view key, std::string_view english,
        std::initializer_list<std::pair<std::string_view, std::string>> values);
    // A count in the language's plural form: catalogs give "one", "few", "many" and "other" as the
    // language needs (CLDR); English is `one` for 1 and `other` otherwise. {count} is filled in.
    [[nodiscard]] static std::string Plural(std::string_view key, std::int64_t count, std::string_view englishOne,
        std::string_view englishOther, std::initializer_list<std::pair<std::string_view, std::string>> values = {});
};

[[nodiscard]] inline std::string_view EditorText(std::string_view key, std::string_view english) {
    return EditorLocalization::Text(key, english);
}

} // namespace kb::editor
