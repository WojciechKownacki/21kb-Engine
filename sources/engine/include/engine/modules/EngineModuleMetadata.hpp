#pragma once

#include "engine/modules/EngineModuleLoadingPhase.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace kb::modules {

// Identity and scheduling information a module reports to the host. Used to resolve
// load order (by loadingPhase, then by dependency edges) and to match against the
// enabled-module set declared in the project descriptor.
struct EngineModuleMetadata {
    std::string name;
    std::uint32_t version = 1U;
    std::vector<std::string> dependencies;
    EngineModuleLoadingPhase loadingPhase = EngineModuleLoadingPhase::Default;
    bool unloadLibraryOnShutdown = true;
};

} // namespace kb::modules
