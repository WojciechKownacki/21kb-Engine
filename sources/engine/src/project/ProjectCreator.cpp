#include "engine/project/ProjectCreator.hpp"

#include "engine/project/ProjectManager.hpp"
#include "engine/project/ProjectSettings.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneDocumentService.hpp"

#include <string>
#include <system_error>

namespace kb::project {
namespace {

[[nodiscard]] ProjectDescriptor MakeDescriptor() {
    ProjectDescriptor descriptor;
    descriptor.contentRoot = "Assets";
    descriptor.targetPlatforms = { "Windows" };
    return descriptor;
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

NewProjectResult ProjectCreator::Create(const std::filesystem::path& parentDirectory, std::string_view name) {
    if (parentDirectory.empty()) {
        return Refuse({}, "Project folder is empty.");
    }
    if (!IsValidName(name)) {
        return Refuse({}, "Project name is invalid: use letters, digits, '-' and '_'.");
    }

    const std::filesystem::path projectRoot = parentDirectory / Utf8Path(name);
    const std::filesystem::path descriptorFile = projectRoot / Utf8Path(std::string{ name } + ".21kbproject");
    // Refused before anything is created: a project the engine could not open afterwards must not
    // be left on disk for the user to find out later.
    if (std::string budget = ProjectManager::PathBudgetError(descriptorFile); !budget.empty()) {
        return Refuse(descriptorFile, std::move(budget));
    }

    std::error_code error;
    if (std::filesystem::exists(descriptorFile, error) && !error) {
        return Refuse(descriptorFile, "Project descriptor already exists in this folder.");
    }

    std::filesystem::create_directories(projectRoot, error);
    if (error) {
        return Refuse(descriptorFile, "Project folder could not be created.");
    }

    const std::filesystem::directory_iterator firstEntry(projectRoot, error);
    if (error) {
        return Refuse(descriptorFile, "Project folder could not be inspected.");
    }
    for (std::filesystem::directory_iterator entry = firstEntry; entry != std::filesystem::directory_iterator{}; entry.increment(error)) {
        if (error) {
            return Refuse(descriptorFile, "Project folder could not be inspected.");
        }
        const std::filesystem::path existing = entry->path();
        if (existing.filename() != "Assets" && existing != descriptorFile) {
            return Refuse(descriptorFile, "Choose an empty project name or remove the existing folder first.");
        }
    }

    std::filesystem::create_directories(projectRoot / "Assets" / "Scenes", error);
    if (error) {
        return Refuse(descriptorFile, "Scene folder could not be created.");
    }
    std::filesystem::create_directories(projectRoot / "Assets" / "Prefabs", error);
    if (error) {
        std::filesystem::remove_all(projectRoot / "Assets", error);
        return Refuse(descriptorFile, "Prefab folder could not be created.");
    }

    ProjectDescriptor descriptor = MakeDescriptor();
    if (!ProjectManager::CreateProject(descriptorFile, descriptor)) {
        std::filesystem::remove_all(projectRoot / "Assets", error);
        return Refuse(descriptorFile, "Project descriptor could not be written.");
    }

    // A new project starts with the settings file the editor and the game both read,
    // rather than waiting for the editor to seed it on first open.
    std::string settingsError;
    if (!ProjectSettingsStore::Save(ProjectSettingsStore::FilePath(projectRoot), MakeSettings(name), settingsError)) {
        std::filesystem::remove_all(projectRoot / "Assets", error);
        std::filesystem::remove(descriptorFile, error);
        return Refuse(descriptorFile, "Project settings could not be written.");
    }

    // The settings name Main.21kbscene as the startup map: it has to exist, or the game and every
    // headless tool start from a scene that is not there.
    kb::scene::Scene startupScene{ descriptor };
    if (!kb::scene::SceneDocumentService::Save(startupScene, projectRoot / "Assets" / "Scenes" / "Main.21kbscene", "Main")) {
        std::filesystem::remove_all(projectRoot / "Assets", error);
        std::filesystem::remove(descriptorFile, error);
        std::filesystem::remove(ProjectSettingsStore::FilePath(projectRoot), error);
        return Refuse(descriptorFile, "Startup scene could not be written.");
    }

    return NewProjectResult{ .succeeded = true, .projectFile = descriptorFile, .error = {} };
}

} // namespace kb::project
