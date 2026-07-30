#pragma once

#include "engine/library/EngineLibraryModuleDesc.hpp"

#include <span>
#include <string>
#include <vector>

namespace kb::script {
class ScriptRuntimeHost;
}

namespace kb::library {

// LIB-028: one line of the startup report — what InstallModules() actually
// did with a single catalog entry, not just whether the whole install
// succeeded. `installed` is true only when capability was true AND
// Register() actually succeeded; `reason` is populated whenever installed
// is false (capability was false — see LibraryModuleDesc::disabledReason
// — the catalog itself failed validation, or Register() failed).
struct EngineLibraryModuleReportEntry {
    std::string name;
    LibraryModuleVersion version;
    std::string ownerRuntime;
    bool capability = true;
    bool installed = false;
    std::string reason;
    kb::core::AllocationTelemetry registrationAllocationTelemetry;
};

// Outcome of installing the Engine21kbLibrary domain modules onto a
// ScriptRuntimeHost. `diagnostics` names exactly the modules that failed to
// register fully; a module can fail partially (some functions registered,
// some rejected as a name collision) without failing the others. `report`
// (LIB-028) has one entry per catalog module, in catalog order, regardless
// of outcome — the actual "available modules, disabled capability,
// versions, reason" startup report the task asks for; `diagnostics` above
// is the older failure-only subset of the same information.
struct EngineLibraryModuleResult {
    bool succeeded = true;
    std::vector<std::string> diagnostics;
    std::vector<EngineLibraryModuleReportEntry> report;
};

// Renders `report` as a stable, human-readable multi-line summary (one
// line per module: name, version, installed or disabled-with-reason) —
// this is the actual printable/loggable "startup report", not just
// structured data nothing ever turns into text. Deterministic given the
// same report (report order is preserved verbatim, no re-sorting).
[[nodiscard]] std::string FormatStartupReport(const std::vector<EngineLibraryModuleReportEntry>& report);

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
