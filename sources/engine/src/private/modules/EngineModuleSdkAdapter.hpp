#pragma once

#include "engine/modules/EngineModuleLoader.hpp"
#include "engine/modules/IEngineModule.hpp"
#include "engine/modules/EngineModuleSdk.h"

#include <memory>

namespace kb::modules {
[[nodiscard]] std::unique_ptr<IEngineModule> CreateSdkModule(
    EngineModuleLibrary library, KbModuleGetApi getApi, std::string_view requestedName,
    std::vector<std::string>& diagnostics);
}
