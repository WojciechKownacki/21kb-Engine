#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kb::project {

struct ProjectModuleDescriptor {
    std::string name;
    std::string type = "Runtime";
    std::string loadingPhase = "Default";
};

struct ProjectPluginReference {
    std::string name;
    std::string binaryPath;
    bool enabled = true;
};

enum class ProjectSceneLightingPath : std::uint32_t {
    Forward = 0U,
    Deferred = 1U,
    ForwardPlus = 2U,
};

struct ProjectDescriptor {
    static constexpr std::uint32_t CurrentFileVersion = 4U;

    std::uint32_t fileVersion = CurrentFileVersion;
    std::string engineAssociation = "21kb";
    std::string name = "Project";
    std::string category;
    std::string description;
    std::string contentRoot = "Assets";
    std::string defaultScene = "/Game/Scenes/Main.21kbscene";
    std::vector<std::string> targetPlatforms;
    std::vector<ProjectModuleDescriptor> modules;
    std::vector<ProjectPluginReference> plugins;
    bool disableEnginePluginsByDefault = false;

    // Project-wide input (file version >= 2). The mapping context activated on
    // play, referenced by its virtual asset path (empty = none).
    std::string inputMappingContext;
    bool inputEnabled = true;

    // Project-wide renderer lighting path (file version >= 4). Older projects
    // remain on Forward until the user switches them in Project Settings.
    ProjectSceneLightingPath sceneLightingPath = ProjectSceneLightingPath::Forward;
};

} // namespace kb::project
