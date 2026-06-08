#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "engine/project/ProjectManager.hpp"
#include "project/EditorProjectBootstrap.hpp"
#include "project/EditorProjectPaths.hpp"

#include <filesystem>
#include <system_error>

namespace {

[[nodiscard]] std::filesystem::path TempRoot() {
    return std::filesystem::temp_directory_path() / "21kb_editor_project_tests";
}

class ScopedCurrentPath {
public:
    explicit ScopedCurrentPath(const std::filesystem::path& path)
        : previous_(std::filesystem::current_path()) {
        std::filesystem::current_path(path);
    }

    ~ScopedCurrentPath() {
        std::error_code error;
        std::filesystem::current_path(previous_, error);
    }

    ScopedCurrentPath(const ScopedCurrentPath&) = delete;
    ScopedCurrentPath& operator=(const ScopedCurrentPath&) = delete;

private:
    std::filesystem::path previous_;
};

void RunProjectBootstrapCreatesDescriptorAndRuntimeFoldersTest() {
    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
    std::filesystem::create_directories(TempRoot(), error);
    kb::editor::tests::Require(!error, "Editor project bootstrap test temp root could not be created");

    const ScopedCurrentPath currentPath{ TempRoot() };

    const kb::editor::EditorProjectBootstrapResult created = kb::editor::EditorProjectBootstrap::BootstrapDefaultProject();
    kb::editor::tests::Require(created.succeeded, "Editor project bootstrap did not create a project descriptor");
    kb::editor::tests::Require(created.created, "Editor project bootstrap should report first-run creation");
    kb::editor::tests::Require(std::filesystem::is_regular_file(kb::editor::EditorProjectPaths::ProjectFile()), "Editor project descriptor file was not created");
    kb::editor::tests::Require(std::filesystem::is_directory(kb::editor::EditorProjectPaths::ScenesRoot()), "Editor project scenes folder was not created");
    kb::editor::tests::Require(std::filesystem::is_directory(kb::editor::EditorProjectPaths::PrefabsRoot()), "Editor project prefabs folder was not created");
    kb::editor::tests::Require(created.descriptor.defaultScene == "/Game/Scenes/Main.21kbscene", "Editor project default scene virtual path is invalid");

    const kb::project::ProjectDescriptorReadResult loaded = kb::project::ProjectManager::LoadProject(kb::editor::EditorProjectPaths::ProjectFile());
    kb::editor::tests::Require(loaded.succeeded, "Created editor project descriptor did not load");
    kb::editor::tests::Require(loaded.descriptor.name == "Project", "Created editor project descriptor name is invalid");
    kb::editor::tests::Require(loaded.descriptor.contentRoot == "Assets", "Created editor project content root is invalid");

    const kb::editor::EditorProjectBootstrapResult reopened = kb::editor::EditorProjectBootstrap::BootstrapDefaultProject();
    kb::editor::tests::Require(reopened.succeeded, "Editor project bootstrap did not reopen an existing project descriptor");
    kb::editor::tests::Require(!reopened.created, "Editor project bootstrap should not recreate an existing descriptor");

    std::filesystem::current_path(TempRoot().parent_path(), error);
    std::filesystem::remove_all(TempRoot(), error);
}

} // namespace

namespace kb::editor::tests {

void RunEditorProjectTests() {
    RunProjectBootstrapCreatesDescriptorAndRuntimeFoldersTest();
}

} // namespace kb::editor::tests
