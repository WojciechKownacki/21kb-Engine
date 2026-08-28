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
#include <fstream>
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

[[nodiscard]] std::string ReadFileBytes(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    return { std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
}

void WriteTextFile(const std::filesystem::path& path, std::string_view text) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    output << text;
}

void RunProjectBootstrapCreatesDescriptorAndRuntimeFoldersTest() {
    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
    std::filesystem::create_directories(TempRoot(), error);
    kb::editor::tests::Require(!error, "Editor project bootstrap test temp root could not be created");

    kb::editor::EditorProjectPaths::SetProjectFile({});
    const ScopedCurrentPath currentPath{ TempRoot() };

    const kb::editor::EditorProjectBootstrapResult created = kb::editor::EditorProjectBootstrap::BootstrapDefaultProject();
    kb::editor::tests::Require(created.succeeded, "Editor project bootstrap did not create a project descriptor");
    kb::editor::tests::Require(created.created, "Editor project bootstrap should report first-run creation");
    kb::editor::tests::Require(std::filesystem::is_regular_file(kb::editor::EditorProjectPaths::ProjectFile()), "Editor project descriptor file was not created");
    kb::editor::tests::Require(std::filesystem::is_directory(kb::editor::EditorProjectPaths::ScenesRoot()), "Editor project scenes folder was not created");
    kb::editor::tests::Require(std::filesystem::is_directory(kb::editor::EditorProjectPaths::PrefabsRoot()), "Editor project prefabs folder was not created");
    kb::editor::tests::Require(created.settings.defaultMap == "/Game/Scenes/Main.21kbscene", "Editor project default map is invalid");
    kb::editor::tests::Require(HasEnabledPlugin(created.descriptor, "Physics.Jolt"), "Editor project default physics plugin was not configured");
    kb::editor::tests::Require(HasEnabledPlugin(created.descriptor, "Audio.Miniaudio"), "Editor project default audio plugin was not configured");
    kb::editor::tests::Require(HasEnabledPlugin(created.descriptor, "Rendering.BasicLighting"), "Editor project default lighting plugin was not configured");
    kb::editor::tests::Require(BinaryPathFor(created.descriptor, "Physics.Jolt") == kb::editor::EditorPluginCatalog::PersistentBinaryPath("Physics.Jolt"), "Editor project bootstrap should persist a config-agnostic physics plugin path");
    kb::editor::tests::Require(BinaryPathFor(created.descriptor, "Audio.Miniaudio") == kb::editor::EditorPluginCatalog::PersistentBinaryPath("Audio.Miniaudio"), "Editor project bootstrap should persist a config-agnostic audio plugin path");
    kb::editor::tests::Require(BinaryPathFor(created.descriptor, "Rendering.BasicLighting") == kb::editor::EditorPluginCatalog::PersistentBinaryPath("Rendering.BasicLighting"), "Editor project bootstrap should persist a config-agnostic lighting plugin path");

    const kb::project::ProjectDescriptorReadResult loaded = kb::project::ProjectManager::LoadProject(kb::editor::EditorProjectPaths::ProjectFile());
    kb::editor::tests::Require(loaded.succeeded, "Created editor project descriptor did not load");
    kb::editor::tests::Require(created.settings.name == "Project", "Created editor project name is invalid");
    kb::editor::tests::Require(loaded.descriptor.contentRoot == "Assets", "Created editor project content root is invalid");
    kb::editor::tests::Require(HasEnabledPlugin(loaded.descriptor, "Physics.Jolt"), "Created editor project descriptor did not persist the default physics plugin");
    kb::editor::tests::Require(HasEnabledPlugin(loaded.descriptor, "Audio.Miniaudio"), "Created editor project descriptor did not persist the default audio plugin");
    kb::editor::tests::Require(HasEnabledPlugin(loaded.descriptor, "Rendering.BasicLighting"), "Created editor project descriptor did not persist the default lighting plugin");
    kb::editor::tests::Require(BinaryPathFor(loaded.descriptor, "Physics.Jolt") == kb::editor::EditorPluginCatalog::PersistentBinaryPath("Physics.Jolt"), "Persisted physics plugin path should stay config-agnostic");
    kb::editor::tests::Require(BinaryPathFor(loaded.descriptor, "Audio.Miniaudio") == kb::editor::EditorPluginCatalog::PersistentBinaryPath("Audio.Miniaudio"), "Persisted audio plugin path should stay config-agnostic");
    kb::editor::tests::Require(BinaryPathFor(loaded.descriptor, "Rendering.BasicLighting") == kb::editor::EditorPluginCatalog::PersistentBinaryPath("Rendering.BasicLighting"), "Persisted lighting plugin path should stay config-agnostic");
    kb::editor::tests::Require(created.settings.lightingPath == kb::project::ProjectSceneLightingPath::Forward, "Created editor project should default to Forward lighting path");
    kb::editor::tests::Require(loaded.descriptor.fileVersion == kb::project::ProjectDescriptor::CurrentFileVersion,
        "Created editor project descriptor should be written at the current file version");

    // The lighting path is a setting now, so it round-trips through the settings file.
    const std::filesystem::path settingsFile =
        kb::project::ProjectSettingsStore::FilePath(kb::editor::EditorProjectPaths::ProjectRoot());
    kb::editor::tests::Require(std::filesystem::is_regular_file(settingsFile), "Editor project settings file was not created");
    for (const kb::project::ProjectSceneLightingPath path : {
             kb::project::ProjectSceneLightingPath::ForwardPlus,
             kb::project::ProjectSceneLightingPath::Deferred }) {
        kb::project::ProjectSettings settings = created.settings;
        settings.lightingPath = path;
        std::string settingsError;
        kb::editor::tests::Require(kb::project::ProjectSettingsStore::Save(settingsFile, settings, settingsError),
            "Editor project settings did not write the lighting path");
        const auto reloaded = kb::project::ProjectSettingsStore::Load(settingsFile);
        kb::editor::tests::Require(reloaded.Succeeded() && reloaded.found && reloaded.settings.lightingPath == path,
            "Lighting path did not roundtrip through the project settings file");
    }

    const kb::editor::EditorProjectBootstrapResult reopened = kb::editor::EditorProjectBootstrap::BootstrapDefaultProject();
    kb::editor::tests::Require(reopened.succeeded, "Editor project bootstrap did not reopen an existing project descriptor");
    kb::editor::tests::Require(!reopened.created, "Editor project bootstrap should not recreate an existing descriptor");

    kb::editor::EditorProjectPaths::SetProjectFile({});
    std::filesystem::current_path(TempRoot().parent_path(), error);
    std::filesystem::remove_all(TempRoot(), error);
}

void RunProjectPathsPreferRepositoryProjectWhenLaunchedFromBuildTreeTest() {
    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
    const std::filesystem::path repoRoot = TempRoot() / "DevRepo";
    const std::filesystem::path repoProject = repoRoot / "Project";
    const std::filesystem::path buildProject = repoRoot / "build" / "bin" / "Debug" / "Project";
    const std::filesystem::path launchDir = repoRoot / "build" / "bin" / "Debug";
    std::filesystem::create_directories(launchDir, error);
    kb::editor::tests::Require(!error, "Editor project path launch dir could not be created");
    WriteTextFile(repoRoot / "sources" / "editor" / "src" / "project" / "EditorProjectPaths.cpp", "// sentinel\n");

    kb::project::ProjectDescriptor repoDescriptor{};
    repoDescriptor.contentRoot = "Assets";
    const kb::project::ProjectDescriptor buildDescriptor = repoDescriptor;
    kb::editor::tests::Require(kb::project::ProjectManager::CreateProject(repoProject / "Project.21kbproject", repoDescriptor),
        "Editor project path test could not create repository project");
    kb::editor::tests::Require(kb::project::ProjectManager::CreateProject(buildProject / "Project.21kbproject", buildDescriptor),
        "Editor project path test could not create stale build project");

    kb::editor::EditorProjectPaths::SetProjectFile({});
    const ScopedCurrentPath currentPath{ launchDir };
    kb::editor::tests::Require(kb::editor::EditorProjectPaths::ProjectRoot() == repoProject,
        "Editor project paths should prefer the repository Project over build/bin/Debug/Project when launched from a build tree");
    kb::editor::tests::Require(kb::editor::EditorProjectPaths::ProjectFile() == repoProject / "Project.21kbproject",
        "Editor project descriptor path should resolve to the repository project from build/bin/Debug");

    kb::editor::EditorProjectPaths::SetProjectFile({});
    std::filesystem::current_path(TempRoot().parent_path(), error);
    std::filesystem::remove_all(TempRoot(), error);
}

void RunDefaultSceneFactorySeedsEmptySceneTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity selected = kb::editor::EditorDefaultSceneFactory::Seed(scene);
    kb::editor::tests::Require(!selected.IsValid(), "Editor default scene should not select an entity in an empty scene");
    kb::editor::tests::Require(scene.Hierarchy().RootEntities().empty(), "Editor default scene should start without root entities");
}

void RunParticleProviderMigrationPolicyTest() {
    std::error_code error;
    const std::filesystem::path root = TempRoot() / "ParticleMigration";
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root / "Assets", error);
    kb::editor::tests::Require(!error, "Particle provider migration fixture could not be created");
    const ScopedCurrentPath currentPath{ root };
    kb::editor::EditorProjectPaths::SetProjectFile(root / "Project.21kbproject");

    kb::project::ProjectDescriptor descriptor;
    descriptor.plugins = {
        { .name = "Physics.Jolt", .binaryPath = kb::editor::EditorPluginCatalog::PersistentBinaryPath("Physics.Jolt"), .enabled = true },
        { .name = "Audio.Miniaudio", .binaryPath = kb::editor::EditorPluginCatalog::PersistentBinaryPath("Audio.Miniaudio"), .enabled = true },
        { .name = "Rendering.BasicLighting", .binaryPath = kb::editor::EditorPluginCatalog::PersistentBinaryPath("Rendering.BasicLighting"), .enabled = true },
    };
    kb::editor::tests::Require(kb::project::ProjectManager::SaveProject(kb::editor::EditorProjectPaths::ProjectFile(), descriptor),
        "Particle provider migration descriptor could not be saved");
    WriteTextFile(root / "Assets" / "Existing.kbvfx", "21kb ParticleEffect 2\n");

    const std::string before = ReadFileBytes(kb::editor::EditorProjectPaths::ProjectFile());
    const auto timestamp = std::filesystem::last_write_time(kb::editor::EditorProjectPaths::ProjectFile(), error);
    kb::editor::EditorProjectBootstrapResult pending = kb::editor::EditorProjectBootstrap::BootstrapDefaultProject();
    kb::editor::tests::Require(pending.succeeded && !pending.created &&
            pending.particlePolicy.requirement == kb::project::ParticleProjectRequirement::Missing,
        "Existing project did not expose the explicit particle provider Add/Cancel migration");
    kb::editor::tests::Require(ReadFileBytes(kb::editor::EditorProjectPaths::ProjectFile()) == before &&
            std::filesystem::last_write_time(kb::editor::EditorProjectPaths::ProjectFile(), error) == timestamp,
        "Cancel/no-action silently changed the existing project descriptor");

    kb::editor::tests::Require(kb::editor::EditorProjectBootstrap::AcceptParticleProvider(pending.projectFile, pending.descriptor),
        "Explicit particle provider Add could not persist the project descriptor");
    const kb::project::ProjectDescriptorReadResult loaded = kb::project::ProjectManager::LoadProject(pending.projectFile);
    kb::editor::tests::Require(loaded.succeeded && HasEnabledPlugin(loaded.descriptor, "Rendering.21kbParticle") &&
            BinaryPathFor(loaded.descriptor, "Rendering.21kbParticle") ==
                kb::editor::EditorPluginCatalog::PersistentBinaryPath("Rendering.21kbParticle"),
        "Explicit particle provider Add persisted the wrong provider reference");

    kb::editor::EditorProjectPaths::SetProjectFile({});
    std::filesystem::current_path(TempRoot(), error);
    std::filesystem::remove_all(root, error);
}

void RunTerrainEditorPluginCatalogTest() {
    const kb::editor::EditorPluginDescriptor* plugin =
        kb::editor::EditorPluginCatalog::FindById("Editor.Terrain");
    kb::editor::tests::Require(plugin != nullptr, "Terrain Editor plugin should be present in the editor catalog");
    kb::editor::tests::Require(plugin->displayName == "Terrain Editor", "Terrain Editor catalog display name is invalid");
    kb::editor::tests::Require(plugin->category == "Editor", "Terrain Editor catalog category is invalid");
    kb::editor::tests::Require(
        kb::editor::EditorPluginCatalog::PersistentBinaryPath("Editor.Terrain") ==
#if defined(_WIN32)
            "kb_terrain_editor_plugin.dll",
#else
            "libkb_terrain_editor_plugin.so",
#endif
        "Terrain Editor catalog binary path should be configuration agnostic");
}

} // namespace

namespace kb::editor::tests {

void RunEditorProjectTests() {
    RunProjectBootstrapCreatesDescriptorAndRuntimeFoldersTest();
    RunProjectPathsPreferRepositoryProjectWhenLaunchedFromBuildTreeTest();
    RunParticleProviderMigrationPolicyTest();
    RunDefaultSceneFactorySeedsEmptySceneTest();
    RunTerrainEditorPluginCatalogTest();
}

} // namespace kb::editor::tests
