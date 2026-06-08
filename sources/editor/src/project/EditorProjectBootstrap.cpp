#include "project/EditorProjectBootstrap.hpp"

#include "engine/project/ProjectManager.hpp"
#include "project/EditorProjectPaths.hpp"

#include <system_error>

namespace kb::editor {
namespace {

[[nodiscard]] kb::project::ProjectDescriptor DefaultDescriptor() {
    kb::project::ProjectDescriptor descriptor;
    const std::string projectName = EditorProjectPaths::ProjectFile().stem().string();
    descriptor.name = projectName.empty() ? "Project" : projectName;
    descriptor.category = "Game";
    descriptor.description = "21kb editor project";
    descriptor.contentRoot = "Assets";
    descriptor.defaultScene = "/Game/Scenes/Main.21kbscene";
    descriptor.targetPlatforms = { "Windows" };
    return descriptor;
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
        return EditorProjectBootstrapResult{
            .succeeded = loaded.succeeded,
            .descriptor = loaded.descriptor,
            .projectFile = projectFile,
            .error = loaded.error,
            .created = false,
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

} // namespace kb::editor
