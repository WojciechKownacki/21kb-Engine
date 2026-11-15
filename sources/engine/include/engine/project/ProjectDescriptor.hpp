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

// How a project's opaque surfaces are lit. Auto leaves the path to the platform: Deferred on computers, Forward
// on phones and tablets, where the G-buffer and the full-screen lighting pass cost more memory bandwidth than a
// handheld GPU can spare. The other values name a path and are used as written. The numbers are stored in
// packages, so new paths are added at the end.
enum class ProjectSceneLightingPath : std::uint32_t {
    Forward = 0U,
    Deferred = 1U,
    ForwardPlus = 2U,
    Auto = 3U,
};

// The path a device draws with: a named path as it is, Auto as the device's own. Never returns Auto.
[[nodiscard]] constexpr ProjectSceneLightingPath ResolveProjectSceneLightingPath(
    ProjectSceneLightingPath path, bool mobile) noexcept {
    if (path != ProjectSceneLightingPath::Auto) {
        return path;
    }
    return mobile ? ProjectSceneLightingPath::Forward : ProjectSceneLightingPath::Deferred;
}

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
