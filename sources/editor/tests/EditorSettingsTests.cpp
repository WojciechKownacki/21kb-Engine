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
    written.particleEditor.visible = false;
    written.particleEditor.area = kb::editor::DockArea::Floating;
    written.particleEditor.floatingRect = kb::editor::DockRect{ 12, 34, 940, 620 };
    written.particleEditor.documentPath = root / "Assets" / "Open.kbvfx";

    std::string saveError;
    kb::editor::tests::Require(kb::editor::EditorConfigurationStore::Save(path, root, written, saveError),
        "Editor settings could not be saved");
    kb::editor::tests::Require(saveError.empty(), "A successful save should report no error");
    kb::editor::tests::Require(path.filename() == "EditorSettings.ini" && path.parent_path().filename() == "Config",
        "Editor settings belong in the project's Config directory");

    const auto loaded = kb::editor::EditorConfigurationStore::Load(path, root);
    kb::editor::tests::Require(loaded.Succeeded() && loaded.found, "Saved editor settings were not read back");
    const kb::editor::EditorConfiguration& read = loaded.configuration;
    kb::editor::tests::Require(
        read.saving == written.saving &&
            read.particleEditor.visible == written.particleEditor.visible &&
            read.particleEditor.area == written.particleEditor.area &&
            read.particleEditor.floatingRect.x == written.particleEditor.floatingRect.x &&
            read.particleEditor.floatingRect.y == written.particleEditor.floatingRect.y &&
            read.particleEditor.floatingRect.width == written.particleEditor.floatingRect.width &&
            read.particleEditor.floatingRect.height == written.particleEditor.floatingRect.height &&
            read.particleEditor.documentPath.lexically_normal() ==
                written.particleEditor.documentPath.lexically_normal(),
        "Editor settings did not survive a save and load unchanged");
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
    configuration.particleEditor.documentPath = root.parent_path() / "Outside.kbvfx";
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
