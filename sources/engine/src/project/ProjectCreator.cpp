#include "engine/project/ProjectCreator.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/project/ProjectManager.hpp"
#include "engine/project/ProjectSettings.hpp"

#include <string>
#include <system_error>

namespace kb::project {
namespace {

[[nodiscard]] std::string PluginFileName(std::string_view stem) {
#if defined(_WIN32)
    return std::string{ stem } + ".dll";
#else
    return "lib" + std::string{ stem } + ".so";
#endif
}

[[nodiscard]] ProjectSettings MakeSettings(std::string_view name) {
    ProjectSettings settings;
    settings.name = std::string{ name };
    settings.gameName = settings.name;
    settings.category = "Game";
    settings.description = "21kb project";
    settings.defaultMap = "/Game/Scenes/Main.21kbscene"; // written by Create() below
    return settings;
}

// The name is UTF-8; a plain std::string would be read in the Windows code page.
[[nodiscard]] std::filesystem::path Utf8Path(std::string_view text) {
    return std::filesystem::path{ std::u8string{ reinterpret_cast<const char8_t*>(text.data()), text.size() } };
}

[[nodiscard]] NewProjectResult Refuse(std::filesystem::path projectFile, std::string error) {
    return NewProjectResult{ .succeeded = false, .projectFile = std::move(projectFile), .error = std::move(error) };
}

} // namespace

ProjectDescriptor ProjectCreator::DefaultDescriptor() {
    ProjectDescriptor descriptor;
    descriptor.contentRoot = "Assets";
    descriptor.targetPlatforms = { "Windows" };
    for (const auto& [name, stem] : { std::pair{ "Physics.Jolt", "kb_physics_jolt_plugin" },
             std::pair{ "Audio.Miniaudio", "kb_audio_miniaudio_plugin" },
             std::pair{ "Rendering.BasicLighting", "kb_basic_lighting_plugin" },
             std::pair{ "Rendering.21kbParticle", "kb_21kb_particle_plugin" } }) {
        descriptor.plugins.push_back(ProjectPluginReference{ .name = name, .binaryPath = PluginFileName(stem), .enabled = true });
    }
    return descriptor;
}

bool ProjectCreator::IsValidName(std::string_view name) noexcept {
    if (name.empty()) {
        return false;
    }
    for (const char character : name) {
        const bool letter = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
        const bool digit = character >= '0' && character <= '9';
        // UTF-8 bytes of non-ASCII letters (the Hub keeps names like "Wyścig").
        const bool international = static_cast<unsigned char>(character) >= 0x80U;
        if (!letter && !digit && !international && character != '-' && character != '_') {
            return false;
        }
    }
    return true;
}

NewProjectResult ProjectCreator::Create(const std::filesystem::path& parentDirectory, std::string_view name,
    kb::scene::SceneTemplate startupTemplate) {
    if (parentDirectory.empty()) {
        return Refuse({}, "Choose the folder the project is created in.");
    }
    if (!IsValidName(name)) {
        return Refuse({}, "Project name is invalid: use letters, digits, '-' and '_'.");
    }
    if (std::string problem = kb::assets::AssetFileNameError(name); !problem.empty()) {
        return Refuse({}, "Project name is invalid: " + problem);
    }

    const std::filesystem::path projectRoot = parentDirectory / Utf8Path(name);
    const std::filesystem::path descriptorFile = projectRoot / Utf8Path(std::string{ name } + ".21kbproject");
    // Refused before anything is created: a project the engine could not open afterwards must not
    // be left on disk for the user to find out later.
    if (std::string budget = ProjectManager::PathBudgetError(descriptorFile); !budget.empty()) {
        return Refuse(descriptorFile, std::move(budget));
    }

    // Like Unity Hub and Unreal: a project goes into a new or an empty folder, never over files
    // the user already has (an earlier project, its Assets).
    std::error_code error;
    const bool rootExisted = std::filesystem::exists(projectRoot, error);
    if (error) {
        return Refuse(descriptorFile, "The project folder could not be inspected: " + error.message());
    }
    if (rootExisted) {
        if (!std::filesystem::is_directory(projectRoot, error)) {
            return Refuse(descriptorFile, "A file with the project's name is in the way. Choose another name.");
        }
        if (!std::filesystem::is_empty(projectRoot, error) || error) {
            return Refuse(descriptorFile, "The folder " + projectRoot.filename().string() +
                " already exists and is not empty. Choose another name or another location.");
        }
    }
    std::filesystem::create_directories(projectRoot, error);
    if (error) {
        return Refuse(descriptorFile, "The project folder could not be created: " + error.message());
    }
    // Everything inside the folder is ours from here on, so a failure takes it all back.
    const auto undo = [&projectRoot, rootExisted]() {
        std::error_code ignored;
        if (rootExisted) {
            for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(projectRoot, ignored)) {
                std::filesystem::remove_all(entry.path(), ignored);
            }
        } else {
            std::filesystem::remove_all(projectRoot, ignored);
        }
    };

    std::filesystem::create_directories(projectRoot / "Assets" / "Scenes", error);
    if (error) {
        undo();
        return Refuse(descriptorFile, "The scene folder could not be created: " + error.message());
    }
    std::filesystem::create_directories(projectRoot / "Assets" / "Prefabs", error);
    if (error) {
        undo();
        return Refuse(descriptorFile, "The prefab folder could not be created: " + error.message());
    }

    ProjectDescriptor descriptor = DefaultDescriptor();
    if (!ProjectManager::CreateProject(descriptorFile, descriptor)) {
        undo();
        return Refuse(descriptorFile, "The project file could not be written.");
    }

    // A new project starts with the settings file the editor and the game both read,
    // rather than waiting for the editor to seed it on first open.
    std::string settingsError;
    if (!ProjectSettingsStore::Save(ProjectSettingsStore::FilePath(projectRoot), MakeSettings(name), settingsError)) {
        undo();
        return Refuse(descriptorFile, "The project settings could not be written: " + settingsError);
    }

    // The settings name Main.21kbscene as the startup map: it has to exist, or the game and every
    // headless tool start from a scene that is not there.
    if (std::string written = kb::scene::SceneTemplates::CreateFile(
            projectRoot / "Assets" / "Scenes" / "Main.21kbscene", startupTemplate, descriptor);
        !written.empty()) {
        undo();
        return Refuse(descriptorFile, "The startup scene could not be written: " + written);
    }

    return NewProjectResult{ .succeeded = true, .projectFile = descriptorFile, .error = {} };
}

} // namespace kb::project
