#include "localization/EditorLocalization.hpp"

#include "engine/config/IniDocument.hpp"
#include "engine/localization/LocalizationCatalogIO.hpp"
#include "engine/localization/PluralRules.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#else
#include <cstdlib>
#endif

namespace kb::editor {
namespace {

// The languages of the moonswar client, which the editors follow.
constexpr std::array<EditorLanguage, 33> kLanguages{ {
    { "en", "English", "English" },
    { "pl", "Polski", "Polish" },
    { "de", "Deutsch", "German" },
    { "fr", "Fran\xC3\xA7" "ais", "French" },
    { "es", "Espa\xC3\xB1ol", "Spanish" },
    { "it", "Italiano", "Italian" },
    { "ru", "\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9", "Russian" },
    { "ja", "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E", "Japanese" },
    { "zh-Hans", "\xE7\xAE\x80\xE4\xBD\x93\xE4\xB8\xAD\xE6\x96\x87", "Chinese (Simplified)" },
    { "ko", "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4", "Korean" },
    { "pt-PT", "Portugu\xC3\xAAs (Portugal)", "Portuguese (Portugal)" },
    { "pt-BR", "Portugu\xC3\xAAs (Brasil)", "Portuguese (Brazil)" },
    { "nl", "Nederlands", "Dutch" },
    { "cs", "\xC4\x8C" "e\xC5\xA1tina", "Czech" },
    { "hu", "Magyar", "Hungarian" },
    { "ro", "Rom\xC3\xA2n\xC4\x83", "Romanian" },
    { "uk", "\xD0\xA3\xD0\xBA\xD1\x80\xD0\xB0\xD1\x97\xD0\xBD\xD1\x81\xD1\x8C\xD0\xBA\xD0\xB0", "Ukrainian" },
    { "sv", "Svenska", "Swedish" },
    { "tr", "T\xC3\xBCrk\xC3\xA7" "e", "Turkish" },
    { "hi", "\xE0\xA4\xB9\xE0\xA4\xBF\xE0\xA4\xA8\xE0\xA5\x8D\xE0\xA4\xA6\xE0\xA5\x80", "Hindi" },
    { "el", "\xCE\x95\xCE\xBB\xCE\xBB\xCE\xB7\xCE\xBD\xCE\xB9\xCE\xBA\xCE\xAC", "Greek" },
    { "da", "Dansk", "Danish" },
    { "nb", "Norsk bokm\xC3\xA5l", "Norwegian Bokm\xC3\xA5l" },
    { "fi", "Suomi", "Finnish" },
    { "sk", "Sloven\xC4\x8Dina", "Slovak" },
    { "bg", "\xD0\x91\xD1\x8A\xD0\xBB\xD0\xB3\xD0\xB0\xD1\x80\xD1\x81\xD0\xBA\xD0\xB8", "Bulgarian" },
    { "sr", "\xD0\xA1\xD1\x80\xD0\xBF\xD1\x81\xD0\xBA\xD0\xB8", "Serbian" },
    { "hr", "Hrvatski", "Croatian" },
    { "ar", "\xD8\xA7\xD9\x84\xD8\xB9\xD8\xB1\xD8\xA8\xD9\x8A\xD8\xA9", "Arabic" },
    { "id", "Bahasa Indonesia", "Indonesian" },
    { "th", "\xE0\xB9\x84\xE0\xB8\x97\xE0\xB8\xA2", "Thai" },
    { "vi", "Ti\xE1\xBA\xBFng Vi\xE1\xBB\x87t", "Vietnamese" },
    { "ms", "Bahasa Melayu", "Malay" },
} };

struct State {
    std::string language = "en";
    // key -> translated text; plural keys are stored as "key#category".
    std::unordered_map<std::string, std::string> texts;
};

// Every catalog loaded in this run stays in memory, so the text views EditorText hands out never
// dangle after a language switch (a menu or a panel may still hold one).
[[nodiscard]] std::vector<std::unique_ptr<const State>>& LoadedStates() {
    static std::vector<std::unique_ptr<const State>> states;
    return states;
}

[[nodiscard]] const State*& CurrentPointer() {
    static const State english;
    static const State* current = &english;
    return current;
}

[[nodiscard]] const State& CurrentState() {
    return *CurrentPointer();
}

[[nodiscard]] std::string Fill(std::string text, std::initializer_list<std::pair<std::string_view, std::string>> values) {
    for (const auto& [name, value] : values) {
        const std::string placeholder = "{" + std::string{ name } + "}";
        for (std::size_t at = text.find(placeholder); at != std::string::npos; at = text.find(placeholder, at + value.size())) {
            text.replace(at, placeholder.size(), value);
        }
    }
    return text;
}

[[nodiscard]] bool EqualsIgnoreCase(std::string_view left, std::string_view right) noexcept {
    return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(), [](char a, char b) {
        const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
        return lower(a) == lower(b);
    });
}

} // namespace

std::span<const EditorLanguage> EditorLocalization::Languages() noexcept {
    return kLanguages;
}

const EditorLanguage* EditorLocalization::Find(std::string_view code) noexcept {
    const auto found = std::ranges::find_if(kLanguages, [code](const EditorLanguage& language) {
        return EqualsIgnoreCase(language.code, code);
    });
    return found == kLanguages.end() ? nullptr : &*found;
}

std::string_view EditorLocalization::SystemLanguage() {
    std::string tag;
#if defined(_WIN32)
    wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
    if (GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) > 0) {
        for (const wchar_t* character = name; *character != L'\0'; ++character) {
            tag.push_back(*character < 0x80 ? static_cast<char>(*character) : '?');
        }
    }
#else
    for (const char* variable : { "LC_ALL", "LC_MESSAGES", "LANG" }) {
        if (const char* value = std::getenv(variable); value != nullptr && value[0] != '\0') {
            tag = value;
            break;
        }
    }
    tag = tag.substr(0, tag.find('.'));
    std::replace(tag.begin(), tag.end(), '_', '-');
#endif
    // "pt-BR" and "zh-CN" pick their variant; otherwise the language subtag decides.
    if (EqualsIgnoreCase(tag, "pt-BR")) return "pt-BR";
    if (tag.starts_with("zh")) return "zh-Hans";
    const std::string language = tag.substr(0, tag.find('-'));
    if (language == "pt") return "pt-PT";
    if (const EditorLanguage* found = Find(language); found != nullptr) return found->code;
    return "en";
}

std::filesystem::path EditorLocalization::PreferencesFile() {
#if defined(_WIN32)
    PWSTR local = nullptr;
    std::filesystem::path root;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &local)) && local != nullptr) {
        root = std::filesystem::path{ local };
    }
    CoTaskMemFree(local);
#else
    std::filesystem::path root;
    if (const char* config = std::getenv("XDG_CONFIG_HOME"); config != nullptr && config[0] != '\0') {
        root = config;
    } else if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
        root = std::filesystem::path{ home } / ".config";
    }
#endif
    return root.empty() ? std::filesystem::path{} : root / "21kb" / "EditorPreferences.ini";
}

std::string EditorLocalization::PreferredLanguage() {
    return PreferredLanguage(PreferencesFile());
}

bool EditorLocalization::StorePreferredLanguage(std::string_view code, std::string& error) {
    return StorePreferredLanguage(code, PreferencesFile(), error);
}

std::string EditorLocalization::PreferredLanguage(const std::filesystem::path& file) {
    std::error_code missing;
    if (!file.empty() && std::filesystem::exists(file, missing)) {
        kb::config::IniDocument document;
        std::string error;
        if (document.Load(file, error)) {
            if (const std::optional<std::string_view> stored = document.GetString("Editor", "Language");
                stored.has_value() && Find(*stored) != nullptr) {
                return std::string{ Find(*stored)->code };
            }
        }
    }
    return std::string{ SystemLanguage() };
}

bool EditorLocalization::StorePreferredLanguage(std::string_view code, const std::filesystem::path& file,
    std::string& error) {
    const EditorLanguage* language = Find(code);
    if (language == nullptr) {
        error = "Unknown language '" + std::string{ code } + "'.";
        return false;
    }
    if (file.empty()) {
        error = "There is no user folder to keep the editor language in.";
        return false;
    }
    kb::config::IniDocument document;
    std::error_code missing;
    if (std::filesystem::exists(file, missing)) {
        std::string ignored;
        static_cast<void>(document.Load(file, ignored));
    }
    std::filesystem::create_directories(file.parent_path(), missing);
    document.SetString("Editor", "Language", std::string{ language->code });
    return document.Save(file, error);
}

std::string_view EditorLocalization::Current() noexcept {
    return CurrentState().language;
}

std::filesystem::path EditorLocalization::DefaultCatalogFolder() {
#if defined(_WIN32)
    std::wstring module(MAX_PATH, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    module.resize(length);
    return std::filesystem::path{ module }.parent_path() / "Content" / "EditorShell" / "Localization";
#else
    std::error_code error;
    return std::filesystem::read_symlink("/proc/self/exe", error).parent_path() / "Content" / "EditorShell" / "Localization";
#endif
}

bool EditorLocalization::SetLanguage(std::string_view code, const std::filesystem::path& catalogFolder, std::string& error) {
    const EditorLanguage* language = Find(code);
    if (language == nullptr) {
        error = "Unknown language '" + std::string{ code } + "'.";
        return false;
    }
    State next;
    next.language = std::string{ language->code };
    if (next.language != "en") {
        const std::filesystem::path file = catalogFolder / (next.language + ".kbloc");
        const std::optional<kb::localization::LocalizationCatalog> catalog = kb::localization::LocalizationCatalogIO::Load(file);
        if (!catalog.has_value()) {
            error = "The " + std::string{ language->englishName } + " translation could not be read: " + file.generic_string();
            return false;
        }
        const auto entries = catalog->languages.find(next.language);
        if (entries == catalog->languages.end()) {
            error = "The catalog " + file.generic_string() + " has no " + std::string{ language->englishName } + " text.";
            return false;
        }
        for (const auto& [key, message] : entries->second) {
            if (!message.text.empty()) {
                next.texts.emplace(key, message.text);
            }
            for (const auto& [category, text] : message.plurals) {
                next.texts.emplace(key + "#" + category, text);
            }
        }
    }
    LoadedStates().push_back(std::make_unique<const State>(std::move(next)));
    CurrentPointer() = LoadedStates().back().get();
    return true;
}

std::string_view EditorLocalization::Text(std::string_view key, std::string_view english) {
    const State& state = CurrentState();
    if (state.texts.empty()) {
        return english;
    }
    const auto found = state.texts.find(std::string{ key });
    return found == state.texts.end() ? english : std::string_view{ found->second };
}

std::string EditorLocalization::Format(std::string_view key, std::string_view english,
    std::initializer_list<std::pair<std::string_view, std::string>> values) {
    return Fill(std::string{ Text(key, english) }, values);
}

std::string EditorLocalization::Plural(std::string_view key, std::int64_t count, std::string_view englishOne,
    std::string_view englishOther, std::initializer_list<std::pair<std::string_view, std::string>> values) {
    const State& state = CurrentState();
    std::string text{ count == 1 ? englishOne : englishOther };
    if (!state.texts.empty()) {
        const kb::localization::PluralCategory category = kb::localization::SelectPluralCategory(state.language, count);
        const std::string keyed = std::string{ key } + "#" + std::string{ kb::localization::PluralCategoryName(category) };
        if (const auto found = state.texts.find(keyed); found != state.texts.end()) {
            text = found->second;
        } else if (const auto other = state.texts.find(std::string{ key } + "#other"); other != state.texts.end()) {
            text = other->second;
        }
    }
    text = Fill(std::move(text), values);
    return Fill(std::move(text), { { "count", std::to_string(count) } });
}

} // namespace kb::editor
