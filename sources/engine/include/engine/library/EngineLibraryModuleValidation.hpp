#pragma once

#include "engine/library/EngineLibraryModuleDesc.hpp"

#include <span>
#include <string>
#include <vector>

namespace kb::library {

struct ModuleCatalogValidationResult {
    bool succeeded = true;
    std::vector<std::string> errors;
};

// Validates a module catalog before any Register() call runs:
//   - duplicate module names,
//   - a dependency naming a module the catalog does not contain (typo
//     detection),
//   - dependency cycles (DFS over LibraryModuleDesc::dependencies),
//   - a LibraryFunctionDesc::canonicalName (LIB-017) audited by more than
//     one module — the same function claimed as owned by two modules at
//     once, which would mean their signatures/ownership have drifted apart
//     even though ScriptFunctionRegistry itself would only catch the
//     resulting name collision once both modules actually registered.
// Called once, at startup, by EngineLibraryModule::Install() before it
// registers anything — a catalog that fails validation registers nothing,
// rather than partially registering an inconsistent module set.
[[nodiscard]] ModuleCatalogValidationResult ValidateModuleCatalog(std::span<const LibraryModuleDesc> modules);

} // namespace kb::library
