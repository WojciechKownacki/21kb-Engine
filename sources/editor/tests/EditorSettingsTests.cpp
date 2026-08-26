#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "settings/EditorSettingsStore.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

namespace {

[[nodiscard]] std::filesystem::path TempRoot() {
    return std::filesystem::temp_directory_path() / "21kb_editor_settings_tests";
}

void ResetTempRoot() {
    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
}

void RunMissingSettingsUsesDefaultsTest() {
    ResetTempRoot();
    const auto result = kb::editor::EditorSettingsStore::Load(TempRoot() / "missing.txt");
    kb::editor::tests::Require(result.Succeeded(), "Missing editor settings should not be an error");
    kb::editor::tests::Require(!result.found, "Missing editor settings should report no stored profile");
    kb::editor::tests::Require(result.settings.saving.autosaveEnabled, "Default editor settings should enable autosave");
    kb::editor::tests::Require(result.settings.saving.autosaveIntervalMinutes == 10U, "Default autosave interval should be ten minutes");
}

void RunSettingsRoundTripTest() {
    ResetTempRoot();
    kb::editor::EditorSettingsDocument expected;
    expected.saving.autosaveEnabled = false;
    expected.saving.autosaveIntervalMinutes = 30U;

    std::string saveError;
    const std::filesystem::path path = TempRoot() / ".21kb" / "EditorSettings.txt";
    kb::editor::tests::Require(kb::editor::EditorSettingsStore::Save(path, expected, saveError), "Editor settings could not be saved");
    const auto loaded = kb::editor::EditorSettingsStore::Load(path);
    kb::editor::tests::Require(loaded.Succeeded() && loaded.found, "Saved editor settings could not be loaded");
    kb::editor::tests::Require(loaded.settings == expected, "Editor settings round-trip changed values");
    kb::editor::tests::Require(!std::filesystem::exists(path.string() + ".tmp"), "Atomic settings save left a temporary file");

    std::ifstream input{path, std::ios::binary};
    const std::string bytes{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}};
    kb::editor::tests::Require(bytes.find("21kb Editor Settings 2") == 0U, "Editor settings did not use the saving-only file format");
    kb::editor::tests::Require(bytes.find("render_backend") == std::string::npos, "Editor settings still persisted viewport rendering");
    kb::editor::tests::Require(bytes.find("grid_spacing") == std::string::npos, "Editor settings still persisted viewport controls");
    kb::editor::tests::Require(bytes.find("asset_view") == std::string::npos, "Editor settings still persisted Project Files controls");
}

void RunLegacySettingsMigrationTest() {
    ResetTempRoot();
    std::error_code error;
    std::filesystem::create_directories(TempRoot(), error);
    const std::filesystem::path path = TempRoot() / "EditorSettings.txt";
    {
        std::ofstream output{path, std::ios::binary | std::ios::trunc};
        output
            << "21kb Editor Settings 1\n"
            << "render_backend 2\n"
            << "shadows 0\n"
            << "post_process 0\n"
            << "anti_aliasing 3\n"
            << "msaa_samples 8\n"
            << "bloom 0\n"
            << "selection_outline 0\n"
            << "gpu_driven 0\n"
            << "autosave 1\n"
            << "autosave_minutes 30\n"
            << "grid 0\n"
            << "grid_spacing 5\n"
            << "snap 1\n"
            << "snap_step 0.5\n"
            << "rotation_snap 15\n"
            << "asset_recursive 1\n"
            << "asset_view 0\n"
            << "asset_sort 1\n"
            << "asset_folders 0\n"
            << "asset_templates 0\n"
            << "asset_thumbnail_scale 1.35\n";
    }

    const auto loaded = kb::editor::EditorSettingsStore::Load(path);
    kb::editor::tests::Require(loaded.Succeeded() && loaded.found, "Legacy editor settings could not be migrated");
    kb::editor::tests::Require(loaded.settings.saving.autosaveEnabled, "Legacy autosave state was not migrated");
    kb::editor::tests::Require(loaded.settings.saving.autosaveIntervalMinutes == 30U, "Legacy autosave interval was not migrated");
}

void RunMalformedSettingsRejectedTest() {
    ResetTempRoot();
    std::error_code error;
    std::filesystem::create_directories(TempRoot(), error);
    const std::filesystem::path path = TempRoot() / "EditorSettings.txt";
    {
        std::ofstream output{path, std::ios::binary | std::ios::trunc};
        output << "21kb Editor Settings 99\nautosave 1\n";
    }
    const auto loaded = kb::editor::EditorSettingsStore::Load(path);
    kb::editor::tests::Require(!loaded.Succeeded(), "Unsupported editor settings version should be rejected");
    kb::editor::tests::Require(!loaded.found, "Malformed editor settings should not be exposed as loaded");
}

void RunInvalidSettingsAreNotWrittenTest() {
    ResetTempRoot();
    kb::editor::EditorSettingsDocument invalid;
    invalid.saving.autosaveIntervalMinutes = 0U;
    const std::filesystem::path path = TempRoot() / "invalid.txt";
    std::string error;
    kb::editor::tests::Require(!kb::editor::EditorSettingsStore::Save(path, invalid, error), "Invalid editor settings should not be saved");
    kb::editor::tests::Require(!error.empty(), "Invalid editor settings save should explain the failure");
    kb::editor::tests::Require(!std::filesystem::exists(path), "Invalid editor settings save should not create a file");
}

} // namespace

namespace kb::editor::tests {

void RunEditorSettingsTests() {
    RunMissingSettingsUsesDefaultsTest();
    RunSettingsRoundTripTest();
    RunLegacySettingsMigrationTest();
    RunMalformedSettingsRejectedTest();
    RunInvalidSettingsAreNotWrittenTest();
}

} // namespace kb::editor::tests
