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
    bool enabled = true;
};

struct ProjectDescriptor {
    static constexpr std::uint32_t CurrentFileVersion = 2U;

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
};

} // namespace kb::project
