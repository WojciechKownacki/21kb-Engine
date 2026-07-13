#pragma once

#include "engine/library/EngineLibraryModuleDesc.hpp"

#include <span>
#include <string>
#include <vector>

namespace kb::script {
class ScriptRuntimeHost;
}

namespace kb::library {

// Outcome of installing the Engine21kbLibrary domain modules onto a
// ScriptRuntimeHost. `diagnostics` names exactly the modules that failed to
// register fully; a module can fail partially (some functions registered,
// some rejected as a name collision) without failing the others.
struct EngineLibraryModuleResult {
    bool succeeded = true;
    std::vector<std::string> diagnostics;
};

// Single registration entry point for the Engine21kbLibrary public surface
// (namespace kb::library; see EngineLibrary.hpp for the contract and version
// this surface implements). ScriptRuntimeHost calls Install() exactly once
// per world, from RegisterDefaultBackends(), after the Native/Lua/Visual
// Graph backends and bindings already exist. Install() walks Catalog(), a
// data-driven list of LibraryModuleDesc (LIB-016) — the single source of
// truth for which domain modules (Input, Audio, World, Time, Physics,
// Transform, and any module a later LIB-xxx step adds) attach to the host,
// their owner/version/capability. A module whose capability is false is
// skipped, not registered with a fake/no-op implementation (LIB-027).
// Install() never registers a script function itself, so it cannot drift
// from what the modules it walks actually expose.
class EngineLibraryModule final {
public:
    EngineLibraryModule() = delete;

    // The module catalog EngineLibraryModule::Install() walks, in
    // registration order. Exposed so later steps (LIB-020 collision/cycle
    // validation, LIB-022 manifest generation) can read the same data
    // Install() acts on instead of re-deriving it.
    [[nodiscard]] static const std::vector<LibraryModuleDesc>& Catalog();

    [[nodiscard]] static EngineLibraryModuleResult Install(kb::script::ScriptRuntimeHost& host);

    // Installs an arbitrary module list rather than Catalog() — Install()
    // itself is InstallModules(host, Catalog()). Exposed so callers (and
    // tests) can install a custom or partial module set, e.g. to verify
    // that a capability=false entry is skipped rather than registered.
    [[nodiscard]] static EngineLibraryModuleResult InstallModules(kb::script::ScriptRuntimeHost& host, std::span<const LibraryModuleDesc> modules);
};

} // namespace kb::library
