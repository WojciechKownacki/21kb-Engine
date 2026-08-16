#include "project/EditorProjectBootstrap.hpp"

#include "engine/project/ProjectManager.hpp"
#include "project/EditorProjectPaths.hpp"
#include "scene/EditorPluginCatalog.hpp"

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
        return EditorProjectBootstrapResult{
            .succeeded = loaded.succeeded,
            .descriptor = loaded.descriptor,
            .projectFile = projectFile,
            .error = loaded.error,
            .created = false,
            .particlePolicy = particlePolicy,
        };
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

    return EditorProjectBootstrapResult{
        .succeeded = true,
        .descriptor = std::move(descriptor),
        .projectFile = projectFile,
        .error = {},
        .created = true,
    };
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
