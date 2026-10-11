#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "localization/EditorLocalization.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace {

using kb::editor::EditorLocalization;
using kb::editor::EditorText;
using kb::editor::tests::Require;

[[nodiscard]] std::filesystem::path TempRoot() {
    return std::filesystem::temp_directory_path() / "21kb_editor_localization_tests";
}

void Write(const std::filesystem::path& file, std::string_view content) {
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    output << content;
}

void BackToEnglish() {
    std::string error;
    static_cast<void>(EditorLocalization::SetLanguage("en", {}, error));
}

constexpr std::string_view kPolish =
    "schema 1\n"
    "fallback \"pl\"\n"
    "language \"pl\"\n"
    "text \"test.rename\" \"Zmie\xC5\x84 nazw\xC4\x99\"\n"
    "text \"test.saved\" \"Zapisano {scene}\"\n"
    "plural \"test.files\" \"one\" \"{count} plik\"\n"
    "plural \"test.files\" \"few\" \"{count} pliki\"\n"
    "plural \"test.files\" \"many\" \"{count} plik\xC3\xB3w\"\n"
    "plural \"test.files\" \"other\" \"{count} pliku\"\n";

void RunLanguageListTest() {
    const auto languages = EditorLocalization::Languages();
    Require(languages.size() == 33U, "The Language list should hold the 33 editor languages");
    Require(languages[0].code == "en", "English should open the Language list");
    Require(EditorLocalization::Find("pl") != nullptr && EditorLocalization::Find("pl")->nativeName == "Polski",
        "Polish should be listed by its own name");
    Require(EditorLocalization::Find("zh-Hans") != nullptr, "Simplified Chinese should be listed");
    Require(EditorLocalization::Find("tlh") == nullptr, "An unknown language should not be found");
}

void RunEnglishSourceTest() {
    BackToEnglish();
    Require(EditorText("test.rename", "Rename") == "Rename", "English should show the source text");
    Require(EditorLocalization::Format("test.saved", "Saved {scene}", { { "scene", "Main" } }) == "Saved Main",
        "English should fill its placeholders");
    Require(EditorLocalization::Plural("test.files", 1, "{count} file", "{count} files") == "1 file",
        "English should use the one form for 1");
    Require(EditorLocalization::Plural("test.files", 3, "{count} file", "{count} files") == "3 files",
        "English should use the other form for 3");
}

void RunCatalogTranslatesTest() {
    const std::filesystem::path folder = TempRoot() / "catalogs";
    Write(folder / "pl.kbloc", kPolish);
    std::string error;
    Require(EditorLocalization::SetLanguage("pl", folder, error), "The Polish catalog should load");
    Require(EditorLocalization::Current() == "pl", "Polish should be the current language");
    Require(EditorText("test.rename", "Rename") == "Zmie\xC5\x84 nazw\xC4\x99", "A key in the catalog should be translated");
    Require(EditorText("test.not_translated", "Close") == "Close", "A key the catalog lacks should stay English");
    Require(EditorLocalization::Format("test.saved", "Saved {scene}", { { "scene", "Main" } }) == "Zapisano Main",
        "A translation should keep and fill its placeholders");
    Require(EditorLocalization::Plural("test.files", 1, "{count} file", "{count} files") == "1 plik",
        "Polish 1 should use the one form");
    Require(EditorLocalization::Plural("test.files", 3, "{count} file", "{count} files") == "3 pliki",
        "Polish 3 should use the few form");
    Require(EditorLocalization::Plural("test.files", 5, "{count} file", "{count} files") == "5 plik\xC3\xB3w",
        "Polish 5 should use the many form");

    // A view taken before a switch stays readable after it.
    const std::string_view polish = EditorText("test.rename", "Rename");
    BackToEnglish();
    Require(EditorText("test.rename", "Rename") == "Rename", "Switching back should show English again");
    Require(polish == "Zmie\xC5\x84 nazw\xC4\x99", "A text taken before the switch should stay valid");
}

void RunDamagedCatalogRefusedTest() {
    BackToEnglish();
    const std::filesystem::path folder = TempRoot() / "damaged";
    Write(folder / "de.kbloc", "schema 1\nlanguage \"de\"\ntext \"unterminated\n");
    std::string error;
    Require(!EditorLocalization::SetLanguage("de", folder, error), "A damaged catalog should be refused");
    Require(error.find("German") != std::string::npos && error.find("de.kbloc") != std::string::npos,
        "The refusal should name the language and the file");
    Require(EditorLocalization::Current() == "en", "A refused catalog should keep the current language");
    error.clear();
    Require(!EditorLocalization::SetLanguage("fr", folder, error), "A missing catalog should be refused");
    error.clear();
    Require(!EditorLocalization::SetLanguage("tlh", folder, error) && error.find("tlh") != std::string::npos,
        "An unknown language should be refused by name");
}

void RunPreferredLanguageTest() {
    const std::filesystem::path file = TempRoot() / "prefs" / "EditorPreferences.ini";
    Require(EditorLocalization::PreferredLanguage(file) == EditorLocalization::SystemLanguage(),
        "Before a choice the system language should be preferred");
    std::string error;
    Require(EditorLocalization::StorePreferredLanguage("pt-BR", file, error), "The chosen language should be stored");
    Require(EditorLocalization::PreferredLanguage(file) == "pt-BR", "The stored language should be read back");
    Require(!EditorLocalization::StorePreferredLanguage("tlh", file, error) && error.find("tlh") != std::string::npos,
        "An unknown language should not be stored");
    Require(EditorLocalization::PreferredLanguage(file) == "pt-BR", "A refused choice should keep the stored one");
}

void RunShippedCatalogsLoadTest() {
    const std::filesystem::path folder = KB_EDITOR_LOCALIZATION_DIR;
    int loaded = 0;
    for (const auto& language : EditorLocalization::Languages()) {
        if (language.code == "en" || !std::filesystem::exists(folder / (std::string{ language.code } + ".kbloc"))) {
            continue;
        }
        std::string error;
        Require(EditorLocalization::SetLanguage(language.code, folder, error),
            ("A shipped catalog does not load: " + error).c_str());
        ++loaded;
    }
    Require(loaded > 0, "The editor should ship at least one translation");
    BackToEnglish();
}

} // namespace

namespace kb::editor::tests {

void RunEditorLocalizationTests() {
    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
    RunLanguageListTest();
    RunEnglishSourceTest();
    RunCatalogTranslatesTest();
    RunDamagedCatalogRefusedTest();
    RunPreferredLanguageTest();
    RunShippedCatalogsLoadTest();
    BackToEnglish();
    std::filesystem::remove_all(TempRoot(), error);
}

} // namespace kb::editor::tests
