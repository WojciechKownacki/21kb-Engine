#pragma once

#include "engine/core/AllocationBudget.hpp"
#include "engine/library/EngineLibraryFunctionDesc.hpp"

#include <compare>
#include <cstdint>
#include <string>
#include <vector>

namespace kb::script {
class ScriptRuntimeHost;
}

namespace kb::library {

// Semantic version of a single kb::library domain module (World, Input,
// Physics, ...). Independent from LibraryApiVersion (LIB-001 — the whole
// kb::library contract version) and from kb::modules::EngineModuleMetadata
// ::version (engine module load-ordering — an unrelated concept despite
// the similar name).
struct LibraryModuleVersion {
    std::uint16_t major = 0U;
    std::uint16_t minor = 1U;
    std::uint16_t patch = 0U;

    [[nodiscard]] constexpr bool operator==(const LibraryModuleVersion&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const LibraryModuleVersion&) const noexcept = default;
};

// Declares one kb::library domain module (World, Transform, Time, Input,
// Physics, Audio, ...):
//   - name: the module's public identity (matches the prefix of the
//     function names it registers, e.g. "World" for "World.Spawn").
//   - version: this module's own contract version, bumped independently of
//     the other modules and of LibraryApiVersion.
//   - ownerRuntime: a diagnostic label naming the real subsystem that owns
//     the state this module exposes (e.g. "kb::scene::SceneTransforms") —
//     a string for logs/manifests, never a live pointer; the module holds
//     no reference to its owner.
//   - capability: whether this module's backend is actually available in
//     this build/configuration (LIB-027). A module with capability=false
//     is skipped, not registered with a fake/no-op implementation.
//   - dependencies: names of other LibraryModuleDesc entries this module
//     requires, validated for typos/cycles by LIB-020.
//   - Register: the module's existing ScriptRuntimeHost registration entry
//     point (e.g. ScriptWorldApi::Register) — unchanged by this
//     descriptor; the catalog only replaces how EngineLibraryModule::Install
//     walks the module list, not how a module registers its functions.
struct LibraryModuleDesc {
    std::string name;
    LibraryModuleVersion version{};
    std::string ownerRuntime;
    bool capability = true;
    // LIB-028: why capability is false, for the startup report
    // (EngineLibraryModule::InstallModules's report/FormatStartupReport).
    // Empty for every module in Catalog() today (every module's backend is
    // unconditionally compiled into this build, per Catalog()'s own
    // comment) — populated only by a future module whose backend can
    // genuinely be absent (an optional plugin, a platform-specific
    // subsystem). Ignored when capability is true.
    std::string disabledReason;
    std::vector<std::string> dependencies;
    bool (*Register)(kb::script::ScriptRuntimeHost&) = nullptr;

    // Per-function thread affinity/determinism/error metadata (LIB-017),
    // audited incrementally — see LibraryFunctionDesc's own comment for
    // what an empty list means.
    std::vector<LibraryFunctionDesc> functions;
    // Budget for allocations made while this module materializes its public
    // functions in ScriptFunctionRegistry. The registry charges capacity
    // growth to the active module before allocating, so the failing function
    // is rejected before the registry grows past its declared budget.
    std::size_t registrationAllocationBudgetBytes = 64U * 1024U;
};

} // namespace kb::library
