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
//     resulting name collision once both modules actually registered,
//   - a LibraryFunctionDesc::canonicalName not prefixed with its declaring
//     module's own name + '.' (LIB-003) — a hand-authored catalog entry
//     attributed to the wrong module's functions list,
//   - a LibraryFunctionDesc::canonicalName described more than once (within
//     the same module or across modules) with a DIFFERENT threadAffinity,
//     determinism, canFail, inputs, or outputs (LIB-020's "zmiana sygnatur"
//     — signature changes) — this is what catches a copy-pasted entry
//     edited in one place but not the other; it cannot detect drift
//     against the LIVE ScriptApiCatalog (no such catalog exists yet at
//     this pre-registration stage), only internal disagreement within the
//     static catalog itself. Cross-checking against the live catalog is
//     FunctionDescMatchesCatalog (EngineLibraryFunctionDesc.hpp, LIB-017),
//     exercised post-registration by RunFunctionDescCatalogResolvesTest.
// Called once, at startup, by EngineLibraryModule::Install() before it
// registers anything — a catalog that fails validation registers nothing,
// rather than partially registering an inconsistent module set.
[[nodiscard]] ModuleCatalogValidationResult ValidateModuleCatalog(std::span<const LibraryModuleDesc> modules);

} // namespace kb::library
