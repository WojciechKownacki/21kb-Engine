#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "engine/project/ProjectManager.hpp"
#include "engine/project/ProjectDescriptorWriter.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "project/EditorProjectBootstrap.hpp"
#include "project/EditorProjectPaths.hpp"
#include "scene/EditorDefaultSceneFactory.hpp"
#include "scene/EditorPluginCatalog.hpp"

#include <algorithm>
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

[[nodiscard]] bool HasEnabledPlugin(const kb::project::ProjectDescriptor& descriptor, std::string_view name) {
    return std::ranges::any_of(descriptor.plugins, [name](const kb::project::ProjectPluginReference& plugin) {
        return plugin.name == name && plugin.enabled;
    });
}

[[nodiscard]] std::string BinaryPathFor(const kb::project::ProjectDescriptor& descriptor, std::string_view name) {
    const auto iter = std::ranges::find_if(descriptor.plugins, [name](const kb::project::ProjectPluginReference& plugin) {
        return plugin.name == name;
    });
    return iter == descriptor.plugins.end() ? std::string{} : iter->binaryPath;
}

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
    kb::editor::tests::Require(HasEnabledPlugin(created.descriptor, "Physics.Jolt"), "Editor project default physics plugin was not configured");
    kb::editor::tests::Require(HasEnabledPlugin(created.descriptor, "Audio.Miniaudio"), "Editor project default audio plugin was not configured");
    kb::editor::tests::Require(HasEnabledPlugin(created.descriptor, "Rendering.BasicLighting"), "Editor project default lighting plugin was not configured");
    kb::editor::tests::Require(BinaryPathFor(created.descriptor, "Physics.Jolt") == kb::editor::EditorPluginCatalog::PersistentBinaryPath("Physics.Jolt"), "Editor project bootstrap should persist a config-agnostic physics plugin path");
    kb::editor::tests::Require(BinaryPathFor(created.descriptor, "Audio.Miniaudio") == kb::editor::EditorPluginCatalog::PersistentBinaryPath("Audio.Miniaudio"), "Editor project bootstrap should persist a config-agnostic audio plugin path");
    kb::editor::tests::Require(BinaryPathFor(created.descriptor, "Rendering.BasicLighting") == kb::editor::EditorPluginCatalog::PersistentBinaryPath("Rendering.BasicLighting"), "Editor project bootstrap should persist a config-agnostic lighting plugin path");

    const kb::project::ProjectDescriptorReadResult loaded = kb::project::ProjectManager::LoadProject(kb::editor::EditorProjectPaths::ProjectFile());
    kb::editor::tests::Require(loaded.succeeded, "Created editor project descriptor did not load");
    kb::editor::tests::Require(loaded.descriptor.name == "Project", "Created editor project descriptor name is invalid");
    kb::editor::tests::Require(loaded.descriptor.contentRoot == "Assets", "Created editor project content root is invalid");
    kb::editor::tests::Require(HasEnabledPlugin(loaded.descriptor, "Physics.Jolt"), "Created editor project descriptor did not persist the default physics plugin");
    kb::editor::tests::Require(HasEnabledPlugin(loaded.descriptor, "Audio.Miniaudio"), "Created editor project descriptor did not persist the default audio plugin");
    kb::editor::tests::Require(HasEnabledPlugin(loaded.descriptor, "Rendering.BasicLighting"), "Created editor project descriptor did not persist the default lighting plugin");
    kb::editor::tests::Require(BinaryPathFor(loaded.descriptor, "Physics.Jolt") == kb::editor::EditorPluginCatalog::PersistentBinaryPath("Physics.Jolt"), "Persisted physics plugin path should stay config-agnostic");
    kb::editor::tests::Require(BinaryPathFor(loaded.descriptor, "Audio.Miniaudio") == kb::editor::EditorPluginCatalog::PersistentBinaryPath("Audio.Miniaudio"), "Persisted audio plugin path should stay config-agnostic");
    kb::editor::tests::Require(BinaryPathFor(loaded.descriptor, "Rendering.BasicLighting") == kb::editor::EditorPluginCatalog::PersistentBinaryPath("Rendering.BasicLighting"), "Persisted lighting plugin path should stay config-agnostic");
    kb::editor::tests::Require(loaded.descriptor.sceneLightingPath == kb::project::ProjectSceneLightingPath::Forward, "Created editor project should default to Forward lighting path");

    kb::project::ProjectDescriptor deferredDescriptor = loaded.descriptor;
    deferredDescriptor.sceneLightingPath = kb::project::ProjectSceneLightingPath::Deferred;
    kb::editor::tests::Require(kb::project::ProjectDescriptorWriter::Write(kb::editor::EditorProjectPaths::ProjectFile(), deferredDescriptor), "Editor project descriptor did not write Deferred lighting path");
    const kb::project::ProjectDescriptorReadResult deferredLoaded = kb::project::ProjectManager::LoadProject(kb::editor::EditorProjectPaths::ProjectFile());
    kb::editor::tests::Require(deferredLoaded.succeeded, "Deferred editor project descriptor did not reload");
    kb::editor::tests::Require(deferredLoaded.descriptor.sceneLightingPath == kb::project::ProjectSceneLightingPath::Deferred, "Deferred lighting path did not roundtrip through project descriptor");
    kb::editor::tests::Require(deferredLoaded.descriptor.fileVersion >= 4U, "Deferred lighting path descriptor should be version >= 4");

    const kb::editor::EditorProjectBootstrapResult reopened = kb::editor::EditorProjectBootstrap::BootstrapDefaultProject();
    kb::editor::tests::Require(reopened.succeeded, "Editor project bootstrap did not reopen an existing project descriptor");
    kb::editor::tests::Require(!reopened.created, "Editor project bootstrap should not recreate an existing descriptor");

    std::filesystem::current_path(TempRoot().parent_path(), error);
    std::filesystem::remove_all(TempRoot(), error);
}

void RunDefaultSceneFactorySeedsEmptySceneTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity selected = kb::editor::EditorDefaultSceneFactory::Seed(scene);
    kb::editor::tests::Require(!selected.IsValid(), "Editor default scene should not select an entity in an empty scene");
    kb::editor::tests::Require(scene.Hierarchy().RootEntities().empty(), "Editor default scene should start without root entities");
}

} // namespace

namespace kb::editor::tests {

void RunEditorProjectTests() {
    RunProjectBootstrapCreatesDescriptorAndRuntimeFoldersTest();
    RunDefaultSceneFactorySeedsEmptySceneTest();
}

} // namespace kb::editor::tests
