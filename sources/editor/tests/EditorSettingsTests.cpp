#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "settings/EditorSettingsStore.hpp"

#include <filesystem>
#include <fstream>
#include <system_error>

namespace {

[[nodiscard]] std::filesystem::path TempRoot() {
    return std::filesystem::temp_directory_path() / "21kb_editor_settings_tests";
}

void RunMissingSettingsUsesDefaultsTest() {
    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
    const auto result = kb::editor::EditorSettingsStore::Load(TempRoot() / "missing.txt");
    kb::editor::tests::Require(result.Succeeded(), "Missing editor settings should not be an error");
    kb::editor::tests::Require(!result.found, "Missing editor settings should report no stored profile");
    kb::editor::tests::Require(result.settings.workspace.autosaveEnabled, "Default editor settings should enable autosave");
    kb::editor::tests::Require(result.settings.workspace.autosaveIntervalMinutes == 10U, "Default autosave interval should be ten minutes");
}

void RunSettingsRoundTripTest() {
    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
    kb::editor::EditorSettingsDocument expected;
    expected.renderer.renderBackend = kb::editor::EditorRenderBackend::Vulkan;
    expected.renderer.shadowsEnabled = false;
    expected.renderer.postProcessEnabled = false;
    expected.renderer.antiAliasingMode = kb::editor::EditorAntiAliasingMode::Msaa;
    expected.renderer.msaaSamples = 8U;
    expected.renderer.bloomEnabled = false;
    expected.renderer.selectionOutlineEnabled = false;
    expected.renderer.gpuDrivenEnabled = false;
    expected.workspace.autosaveEnabled = true;
    expected.workspace.autosaveIntervalMinutes = 30U;
    expected.workspace.gridVisible = false;
    expected.workspace.gridSpacing = 5.0F;
    expected.workspace.snapEnabled = true;
    expected.workspace.snapStep = 0.5F;
    expected.workspace.rotationSnapDegrees = 15.0F;
    expected.workspace.assetBrowserRecursive = true;
    expected.workspace.assetViewMode = kb::editor::EditorAssetViewMode::List;
    expected.workspace.assetSortMode = kb::editor::EditorAssetSortMode::Type;
    expected.workspace.assetShowFolders = false;
    expected.workspace.assetShowTemplates = false;
    expected.workspace.assetThumbnailScale = 1.35F;

    std::string saveError;
    const std::filesystem::path path = TempRoot() / ".21kb" / "EditorSettings.txt";
    kb::editor::tests::Require(kb::editor::EditorSettingsStore::Save(path, expected, saveError), "Editor settings could not be saved");
    const auto loaded = kb::editor::EditorSettingsStore::Load(path);
    kb::editor::tests::Require(loaded.Succeeded() && loaded.found, "Saved editor settings could not be loaded");
    kb::editor::tests::Require(loaded.settings == expected, "Editor settings round-trip changed values");
    kb::editor::tests::Require(!std::filesystem::exists(path.string() + ".tmp"), "Atomic settings save left a temporary file");
}

void RunMalformedSettingsRejectedTest() {
    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
    std::filesystem::create_directories(TempRoot(), error);
    const std::filesystem::path path = TempRoot() / "EditorSettings.txt";
    {
        std::ofstream output{path, std::ios::binary | std::ios::trunc};
        output << "21kb Editor Settings 99\nrender_backend 0\n";
    }
    const auto loaded = kb::editor::EditorSettingsStore::Load(path);
    kb::editor::tests::Require(!loaded.Succeeded(), "Unsupported editor settings version should be rejected");
    kb::editor::tests::Require(!loaded.found, "Malformed editor settings should not be exposed as loaded");
}

void RunInvalidSettingsAreNotWrittenTest() {
    kb::editor::EditorSettingsDocument invalid;
    invalid.workspace.autosaveIntervalMinutes = 0U;
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
    RunMalformedSettingsRejectedTest();
    RunInvalidSettingsAreNotWrittenTest();
}

} // namespace kb::editor::tests
