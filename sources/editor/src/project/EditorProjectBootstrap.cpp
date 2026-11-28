#include "project/EditorProjectBootstrap.hpp"
#include "engine/project/ProjectCreator.hpp"

#include "engine/project/ProjectManager.hpp"
#include "engine/project/ProjectSettings.hpp"
#include "project/EditorProjectPaths.hpp"
#include "scene/EditorPluginCatalog.hpp"

#include <string_view>
#include <system_error>

namespace kb::editor {
namespace {

[[nodiscard]] bool NormalizeBuiltInPluginPaths(kb::project::ProjectDescriptor& descriptor) {
    bool changed = false;
    for (kb::project::ProjectPluginReference& plugin : descriptor.plugins) {
        changed = EditorPluginCatalog::NormalizeProjectPluginReference(plugin) || changed;
    }
    return changed;
}

[[nodiscard]] bool EnsureProjectDirectories() {
    std::error_code error;
    std::filesystem::create_directories(EditorProjectPaths::AssetsRoot(), error);
    if (error) {
        return false;
    }
    std::filesystem::create_directories(EditorProjectPaths::ScenesRoot(), error);
    if (error) {
        return false;
    }
    std::filesystem::create_directories(EditorProjectPaths::PrefabsRoot(), error);
    return !error;
}

// One read of the project's settings file, at the one moment a project is opened.
// A project that predates the file gets it written from what the descriptor already
// carried, so opening an old project neither loses its configuration nor asks the
// author to restate it.
void LoadOrSeedSettings(EditorProjectBootstrapResult& result) {
    if (!result.succeeded) {
        return;
    }
    const std::filesystem::path settingsFile =
        kb::project::ProjectSettingsStore::FilePath(result.projectFile.parent_path());
    kb::project::ProjectSettingsLoadResult loaded = kb::project::ProjectSettingsStore::Load(settingsFile);
    if (!loaded.Succeeded()) {
        result.settingsError = loaded.error;
        result.settings = kb::project::ProjectSettingsStore::FromLegacy(
            result.legacySettings, result.projectFile);
        return;
    }
    if (loaded.found) {
        result.settings = std::move(loaded.settings);
        // The settings file is the one people edit, so a hand-written change has to
        // reach the descriptor the game and the hub still read. Without this the
        // edit would sit in the file until something in the editor happened to save.
        result.descriptorMirrorStale =
            result.legacySettings.present &&
            kb::project::ProjectSettingsStore::FromLegacy(result.legacySettings, result.projectFile) !=
                result.settings;
        return;
    }

    result.settings = kb::project::ProjectSettingsStore::FromLegacy(
        result.legacySettings, result.projectFile);
    std::string error;
    if (!kb::project::ProjectSettingsStore::Save(settingsFile, result.settings, error)) {
        result.settingsError = error;
    }
}

// A project that predates this layout keeps its old state files around; they are
// no longer read by anything, so they are removed rather than left to confuse.
void RemoveRetiredStateFiles(const std::filesystem::path& projectRoot) {
    std::error_code error;
    for (const std::string_view name : { "EditorSettings.txt", "ParticleEditorSession.txt" }) {
        std::filesystem::remove(projectRoot / ".21kb" / name, error);
    }
}

} // namespace

EditorProjectBootstrapResult EditorProjectBootstrap::BootstrapDefaultProject() {
    const std::filesystem::path projectFile = EditorProjectPaths::ProjectFile();
    bool created = false;
    std::error_code error;
    const bool projectFileExists = std::filesystem::exists(projectFile, error);
    if (error) {
        return EditorProjectBootstrapResult{
            .succeeded = false,
            .descriptor = {},
            .projectFile = projectFile,
            .error = "The project file could not be checked: " + error.message(),
            .created = false,
        };
    }
    if (!projectFileExists) {
        // A folder with no project gets the same new project as the Hub and kb_cli make: descriptor, settings,
        // default mixer and the startup scene the settings name.
        const std::u8string stem = projectFile.stem().u8string();
        const std::string name{ stem.begin(), stem.end() };
        const kb::project::NewProjectResult made =
            kb::project::ProjectCreator::CreateIn(projectFile.parent_path(), name);
        if (!made.succeeded) {
            return EditorProjectBootstrapResult{
                .succeeded = false,
                .descriptor = {},
                .projectFile = projectFile,
                .error = made.error,
                .created = false,
            };
        }
        created = true;
    }

    if (!EnsureProjectDirectories()) {
        return EditorProjectBootstrapResult{
            .succeeded = false,
            .descriptor = {},
            .projectFile = projectFile,
            .error = "Project directories could not be created.",
            .created = created,
        };
    }

    kb::project::ProjectDescriptorReadResult loaded = kb::project::ProjectManager::LoadProject(projectFile);
    if (loaded.succeeded && NormalizeBuiltInPluginPaths(loaded.descriptor)) {
        if (!kb::project::ProjectManager::SaveProject(projectFile, loaded.descriptor)) {
            loaded.succeeded = false;
            loaded.error = "Project descriptor plugin paths could not be normalized.";
        }
    }
    const kb::project::ParticleProjectPolicyResult particlePolicy = loaded.succeeded
        ? kb::project::ParticleProjectPolicy::Inspect(projectFile.parent_path(), loaded.descriptor)
        : kb::project::ParticleProjectPolicyResult{};
    EditorProjectBootstrapResult result{
        .succeeded = loaded.succeeded,
        .descriptor = loaded.descriptor,
        .projectFile = projectFile,
        .error = loaded.error,
        .created = created,
        .particlePolicy = particlePolicy,
        .legacySettings = loaded.legacySettings,
    };
    LoadOrSeedSettings(result);
    RemoveRetiredStateFiles(projectFile.parent_path());
    return result;
}

bool EditorProjectBootstrap::AcceptParticleProvider(
    const std::filesystem::path& projectFile,
    kb::project::ProjectDescriptor& descriptor) {
    kb::project::ProjectDescriptor candidate = descriptor;
    if (!kb::project::ParticleProjectPolicy::Enable(
            candidate,
            EditorPluginCatalog::PersistentBinaryPath(kb::project::ParticleProjectPolicy::PluginId))) {
        return true;
    }
    if (!kb::project::ProjectManager::SaveProject(projectFile, candidate)) return false;
    descriptor = std::move(candidate);
    return true;
}

} // namespace kb::editor
