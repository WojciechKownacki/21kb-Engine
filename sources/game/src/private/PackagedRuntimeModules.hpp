#pragma once

#include "engine/modules/IEngineModule.hpp"
#include "engine/project/ProjectDescriptor.hpp"

#include <memory>
#include <string>
#include <vector>

namespace kb::script {
class ScriptModule;
}

namespace kb::game {

struct PackagedRuntimeModules {
    std::vector<std::unique_ptr<kb::modules::IEngineModule>> modules;
    kb::script::ScriptModule* script = nullptr;
};

// Constructs the providers compiled into every monolithic game host. An enabled
// provider outside the packaged contract is a hard error; a platform package
// must never silently start with a different module graph than the project.
[[nodiscard]] bool CreatePackagedRuntimeModules(
    const kb::project::ProjectDescriptor& descriptor,
    PackagedRuntimeModules& output,
    std::string& error);

} // namespace kb::game
