#pragma once

#include "engine/project/ProjectDescriptor.hpp"

#include <filesystem>
#include <string>

namespace kb::project {

// The project's settings, as opposed to its structure. The descriptor answers
// what a project is made of - engine association, content root, modules, plugins,
// target platforms - and changes when the project is built differently. These
// answer how it is configured, and change while working on it, which is why they
// live in a file a person can open and read.
struct ProjectSettings {
    // Identity
    std::string name = "Project";
    // The name the finished game ships under. The project may be called something
    // else entirely while it is being made.
    std::string gameName;
    std::string category;
    std::string description;

    // Maps
    // The scene the game starts from. A new project points at the scene the editor
    // creates for it, which is where the default used to live before the descriptor
    // stopped carrying settings.
    std::string defaultMap = "/Game/Scenes/Main.21kbscene";
    // The scene the editor had open when it was last closed. Deliberately separate
    // from defaultMap: where an author left off is not where the game begins.
    std::string lastOpenMap;

    // Rendering
    ProjectSceneLightingPath lightingPath = ProjectSceneLightingPath::Forward;

    // Input
    bool inputEnabled = true;
    std::string inputMappingContext;

    // Physics
    std::string physicsLayersAsset;

    [[nodiscard]] bool operator==(const ProjectSettings&) const noexcept = default;
};

// The settings an older project file carried before the settings file existed.
struct ProjectLegacySettings {
    bool present = false;
    std::string name;
    std::string category;
    std::string description;
    std::string defaultScene;
    ProjectSceneLightingPath lightingPath = ProjectSceneLightingPath::Forward;
    bool inputEnabled = true;
    std::string inputMappingContext;
    std::string physicsLayersAsset;
};

struct ProjectSettingsLoadResult {
    ProjectSettings settings{};
    bool found = false;
    std::string error;

    [[nodiscard]] bool Succeeded() const noexcept { return error.empty(); }
};

class ProjectSettingsStore final {
public:
    ProjectSettingsStore() = delete;

    // <project root>/Config/ProjectSettings.ini
    [[nodiscard]] static std::filesystem::path FilePath(const std::filesystem::path& projectRoot);

    // A missing file is not an error: it reports found=false with default settings,
    // so a caller can seed the file from whatever the project already knows.
    [[nodiscard]] static ProjectSettingsLoadResult Load(const std::filesystem::path& path);
    [[nodiscard]] static bool Save(
        const std::filesystem::path& path,
        const ProjectSettings& settings,
        std::string& error);

    // The settings a project carried before it had a settings file, so an existing
    // project keeps its configuration when the file is first created. A project with
    // nothing to carry gets the defaults, named after its own file.
    [[nodiscard]] static ProjectSettings FromLegacy(
        const ProjectLegacySettings& legacy,
        const std::filesystem::path& projectFile);
};

} // namespace kb::project
