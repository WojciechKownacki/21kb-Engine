#include "project/EditorProjectBootstrap.hpp"

#include "engine/project/ProjectManager.hpp"
#include "engine/project/ProjectSettings.hpp"
#include "project/EditorProjectPaths.hpp"
#include "scene/EditorPluginCatalog.hpp"

#include <string_view>
#include <system_error>

namespace kb::editor {
namespace {

#if !defined(KB_PHYSICS_JOLT_PLUGIN_PATH)
#if defined(_WIN32)
#define KB_PHYSICS_JOLT_PLUGIN_PATH "kb_physics_jolt_plugin.dll"
#else
#define KB_PHYSICS_JOLT_PLUGIN_PATH "libkb_physics_jolt_plugin.so"
#endif
#endif

#if !defined(KB_AUDIO_MINIAUDIO_PLUGIN_PATH)
#if defined(_WIN32)
#define KB_AUDIO_MINIAUDIO_PLUGIN_PATH "kb_audio_miniaudio_plugin.dll"
#else
#define KB_AUDIO_MINIAUDIO_PLUGIN_PATH "libkb_audio_miniaudio_plugin.so"
#endif
#endif

#if !defined(KB_BASIC_LIGHTING_PLUGIN_PATH)
#if defined(_WIN32)
#define KB_BASIC_LIGHTING_PLUGIN_PATH "kb_basic_lighting_plugin.dll"
#else
#define KB_BASIC_LIGHTING_PLUGIN_PATH "libkb_basic_lighting_plugin.so"
#endif
#endif

[[nodiscard]] kb::project::ProjectDescriptor DefaultDescriptor() {
    kb::project::ProjectDescriptor descriptor;
    const std::string projectName = EditorProjectPaths::ProjectFile().stem().string();
    descriptor.name = projectName.empty() ? "Project" : projectName;
    descriptor.category = "Game";
    descriptor.description = "21kb editor project";
    descriptor.contentRoot = "Assets";
    descriptor.defaultScene = "/Game/Scenes/Main.21kbscene";
    descriptor.targetPlatforms = { "Windows" };
    descriptor.plugins.push_back(kb::project::ProjectPluginReference{
        .name = "Physics.Jolt",
        .binaryPath = EditorPluginCatalog::PersistentBinaryPath("Physics.Jolt"),
        .enabled = true,
    });
    descriptor.plugins.push_back(kb::project::ProjectPluginReference{
        .name = "Audio.Miniaudio",
        .binaryPath = EditorPluginCatalog::PersistentBinaryPath("Audio.Miniaudio"),
        .enabled = true,
    });
    descriptor.plugins.push_back(kb::project::ProjectPluginReference{
        .name = "Rendering.BasicLighting",
        .binaryPath = EditorPluginCatalog::PersistentBinaryPath("Rendering.BasicLighting"),
        .enabled = true,
    });
    descriptor.plugins.push_back(kb::project::ProjectPluginReference{
        .name = "Rendering.21kbParticle",
        .binaryPath = EditorPluginCatalog::PersistentBinaryPath("Rendering.21kbParticle"),
        .enabled = true,
    });
    return descriptor;
}

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
        result.settings = kb::project::ProjectSettingsStore::FromDescriptor(result.descriptor);
        return;
    }
    if (loaded.found) {
        result.settings = std::move(loaded.settings);
        // The settings file is the one people edit, so a hand-written change has to
        // reach the descriptor the game and the hub still read. Without this the
        // edit would sit in the file until something in the editor happened to save.
        result.descriptorMirrorStale =
            kb::project::ProjectSettingsStore::FromDescriptor(result.descriptor) != result.settings;
        return;
    }

    result.settings = kb::project::ProjectSettingsStore::FromDescriptor(result.descriptor);
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
    if (!EnsureProjectDirectories()) {
        return EditorProjectBootstrapResult{
            .succeeded = false,
            .descriptor = {},
            .projectFile = EditorProjectPaths::ProjectFile(),
            .error = "Project directories could not be created.",
            .created = false,
        };
    }

    const std::filesystem::path projectFile = EditorProjectPaths::ProjectFile();
    std::error_code error;
    if (std::filesystem::exists(projectFile, error) && !error) {
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
            .created = false,
            .particlePolicy = particlePolicy,
        };
        LoadOrSeedSettings(result);
        RemoveRetiredStateFiles(projectFile.parent_path());
        return result;
    }

    kb::project::ProjectDescriptor descriptor = DefaultDescriptor();
    if (!kb::project::ProjectManager::CreateProject(projectFile, descriptor)) {
        return EditorProjectBootstrapResult{
            .succeeded = false,
            .descriptor = {},
            .projectFile = projectFile,
            .error = "Project descriptor could not be created.",
            .created = false,
        };
    }

    EditorProjectBootstrapResult result{
        .succeeded = true,
        .descriptor = std::move(descriptor),
        .projectFile = projectFile,
        .error = {},
        .created = true,
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
