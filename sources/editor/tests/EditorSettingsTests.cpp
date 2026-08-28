#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "settings/EditorConfigurationStore.hpp"

#include <filesystem>
#include <fstream>
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

void RunMissingConfigurationUsesDefaultsTest() {
    ResetTempRoot();
    const auto result = kb::editor::EditorConfigurationStore::Load(
        kb::editor::EditorConfigurationStore::FilePath(TempRoot()), TempRoot());
    kb::editor::tests::Require(result.Succeeded(), "A project without editor settings is not an error");
    kb::editor::tests::Require(!result.found, "A project without editor settings should report nothing stored");
    kb::editor::tests::Require(result.configuration.saving.autosaveEnabled, "Autosave should default to on");
    kb::editor::tests::Require(result.configuration.saving.autosaveIntervalMinutes == 10U,
        "The default autosave interval should be ten minutes");
}

void RunConfigurationRoundTripTest() {
    ResetTempRoot();
    const std::filesystem::path root = TempRoot();
    const std::filesystem::path path = kb::editor::EditorConfigurationStore::FilePath(root);
    std::error_code error;
    std::filesystem::create_directories(root / "Assets", error);

    kb::editor::EditorConfiguration written;
    written.saving.autosaveEnabled = false;
    written.saving.autosaveIntervalMinutes = 45U;
    written.panels.push_back(kb::editor::EditorPanelSession{
        .panelId = 14U,
        .visible = false,
        .area = kb::editor::DockArea::Floating,
        .floatingRect = kb::editor::DockRect{ 12, 34, 940, 620 },
        .documentPath = root / "Assets" / "Open.kbvfx",
    });
    written.panels.push_back(kb::editor::EditorPanelSession{ .panelId = 3U, .visible = true });

    std::string saveError;
    kb::editor::tests::Require(kb::editor::EditorConfigurationStore::Save(path, root, written, saveError),
        "Editor settings could not be saved");
    kb::editor::tests::Require(saveError.empty(), "A successful save should report no error");
    kb::editor::tests::Require(path.filename() == "EditorSettings.ini" && path.parent_path().filename() == "Config",
        "Editor settings belong in the project's Config directory");

    const auto loaded = kb::editor::EditorConfigurationStore::Load(path, root);
    kb::editor::tests::Require(loaded.Succeeded() && loaded.found, "Saved editor settings were not read back");
    const kb::editor::EditorConfiguration& read = loaded.configuration;
    const kb::editor::EditorPanelSession* particle = read.FindPanel(14U);
    const kb::editor::EditorPanelSession* hierarchy = read.FindPanel(3U);
    kb::editor::tests::Require(read.saving == written.saving, "Editor saving preferences did not survive");
    kb::editor::tests::Require(particle != nullptr && hierarchy != nullptr,
        "Every stored panel should come back");
    kb::editor::tests::Require(
        particle->visible == written.panels.front().visible &&
            particle->area == written.panels.front().area &&
            particle->floatingRect.x == written.panels.front().floatingRect.x &&
            particle->floatingRect.y == written.panels.front().floatingRect.y &&
            particle->floatingRect.width == written.panels.front().floatingRect.width &&
            particle->floatingRect.height == written.panels.front().floatingRect.height &&
            particle->documentPath.lexically_normal() ==
                written.panels.front().documentPath.lexically_normal(),
        "A panel's placement did not survive a save and load unchanged");
    kb::editor::tests::Require(hierarchy->documentPath.empty(),
        "A panel without a document should come back without one");
}

void RunConfigurationKeepsForeignKeysTest() {
    ResetTempRoot();
    const std::filesystem::path root = TempRoot();
    const std::filesystem::path path = kb::editor::EditorConfigurationStore::FilePath(root);
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    {
        std::ofstream output{path, std::ios::binary | std::ios::trunc};
        output << "[Editor.Saving]\nAutosave=0\n\n[Editor.Future]\nSomethingNewerWrote=42\n";
    }

    kb::editor::EditorConfiguration configuration;
    configuration.saving.autosaveEnabled = true;
    std::string saveError;
    kb::editor::tests::Require(kb::editor::EditorConfigurationStore::Save(path, root, configuration, saveError),
        "Editor settings could not be rewritten");

    std::ifstream input{path, std::ios::binary};
    const std::string contents{ std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{} };
    kb::editor::tests::Require(contents.find("SomethingNewerWrote=42") != std::string::npos,
        "A key this build does not know must survive being written by it");
    kb::editor::tests::Require(contents.find("Autosave=1") != std::string::npos,
        "The rewritten value should be the one that was saved");
}

void RunConfigurationRejectsDocumentOutsideProjectTest() {
    ResetTempRoot();
    const std::filesystem::path root = TempRoot() / "Project";
    std::error_code error;
    std::filesystem::create_directories(root, error);

    kb::editor::EditorConfiguration configuration;
    configuration.panels.push_back(kb::editor::EditorPanelSession{
        .panelId = 14U,
        .documentPath = root.parent_path() / "Outside.kbvfx",
    });
    std::string saveError;
    kb::editor::tests::Require(
        !kb::editor::EditorConfigurationStore::Save(
            kb::editor::EditorConfigurationStore::FilePath(root), root, configuration, saveError),
        "Editor settings must refuse a document that lives outside the project");
    kb::editor::tests::Require(!saveError.empty(), "A refused save should say why");
}

void RunMalformedConfigurationRejectedTest() {
    ResetTempRoot();
    const std::filesystem::path root = TempRoot();
    const std::filesystem::path path = kb::editor::EditorConfigurationStore::FilePath(root);
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    {
        std::ofstream output{path, std::ios::binary | std::ios::trunc};
        output << "[Editor.Saving\n";
    }

    const auto loaded = kb::editor::EditorConfigurationStore::Load(path, root);
    kb::editor::tests::Require(!loaded.Succeeded(), "A malformed settings file must be reported");
    kb::editor::tests::Require(!loaded.error.empty(), "A rejected settings file should say why");
}

} // namespace

namespace kb::editor::tests {

void RunEditorSettingsTests() {
    RunMissingConfigurationUsesDefaultsTest();
    RunConfigurationRoundTripTest();
    RunConfigurationKeepsForeignKeysTest();
    RunConfigurationRejectsDocumentOutsideProjectTest();
    RunMalformedConfigurationRejectedTest();
    ResetTempRoot();
}

} // namespace kb::editor::tests
