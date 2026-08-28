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

// What a project is structurally made of. Everything a person configures while
// working - identity, maps, rendering, input, physics - lives in the project's
// settings file instead, where it can be read and edited. File version 6 is the
// first without those; older files still open and their settings are handed back
// once, so opening an old project carries them into the settings file.
struct ProjectDescriptor {
    static constexpr std::uint32_t CurrentFileVersion = 6U;

    std::uint32_t fileVersion = CurrentFileVersion;
    std::string engineAssociation = "21kb";
    std::string contentRoot = "Assets";
    std::vector<std::string> targetPlatforms;
    std::vector<ProjectModuleDescriptor> modules;
    std::vector<ProjectPluginReference> plugins;
    bool disableEnginePluginsByDefault = false;
};

} // namespace kb::project
