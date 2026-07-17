#pragma once

#include "engine/library/EngineLibraryDeprecation.hpp"
#include "engine/script/ScriptApiCatalog.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kb::library {

// Where a kb::library function is allowed to run. Every registered
// function runs MainThread today (no worker-safe or render-thread-command
// dispatch path exists yet); this enum exists so LIB-231's per-function
// audit has somewhere real to record its answer instead of leaving thread
// safety undocumented.
enum class LibraryThreadAffinity : std::uint8_t {
    MainThread,
    WorkerSafe,
    RenderThreadCommand,
};

// Whether a function's result is reproducible for the same world state and
// arguments (LIB-237's deterministic subset), or depends on
// non-reproducible state (wall time, platform, async IO, rendering).
enum class LibraryDeterminism : std::uint8_t {
    Deterministic,
    NonDeterministic,
};

// The kb::library-level contract for one registered script function,
// layered on top of kb::script::ScriptFunctionSignature (declared where the
// function is registered via ScriptRuntimeHost::RegisterFunction) rather
// than duplicating it: canonicalName mirrors ScriptFunctionSignature::name
// so a LibraryFunctionDesc always matches back to its
// ScriptFunctionRegistry entry. canFail describes whether the function can
// report a runtime ScriptError (LIB-010) beyond input-validation errors the
// registry itself already rejects (unknown function, type mismatch, missing
// required pin).
//
// inputs/outputs reuse kb::script::ScriptApiPin exactly (LIB-019's own
// precedent: alias/reuse an existing kb::script type rather than
// re-declare its name/type/required shape a second time) rather than
// omitting them — an earlier draft of this type omitted inputs/outputs
// entirely on the theory that ScriptFunctionSignature was the one true
// source and duplicating them would risk drift; the 2026-07-17 audit
// correctly flagged that as leaving "wejścia, wyjścia" (inputs, outputs) —
// an explicit part of this task's own contract — unfulfilled. Recording
// them AND machine-validating them against the live ScriptApiCatalog via
// FunctionDescMatchesCatalog() below (rather than leaving them undeclared)
// is what actually keeps a single source of truth: any drift between a
// recorded LibraryFunctionDesc and its real registration is now something a
// test can catch, not something structurally impossible to state.
//
// Coverage is intentionally incremental: LibraryModuleDesc::functions only
// lists functions that have actually been audited for thread affinity and
// determinism. An empty list means "not yet audited", never "these
// functions don't exist" — ScriptApiCatalog::Build() remains the complete,
// always-current function list. Auditing every function across every
// module is LIB-231 (threadAffinity) and LIB-237 (determinism), not this
// task; this type only has to exist and be provably correct where used.
struct LibraryFunctionDesc {
    std::string canonicalName;
    LibraryThreadAffinity threadAffinity = LibraryThreadAffinity::MainThread;
    LibraryDeterminism determinism = LibraryDeterminism::Deterministic;
    bool canFail = false;
    std::vector<kb::script::ScriptApiPin> inputs;
    std::vector<kb::script::ScriptApiPin> outputs;
    // LIB-025: std::nullopt for every function today — nothing in this
    // engine is actually being phased out yet, and marking a live function
    // deprecated just to exercise this field would be a lie the warning
    // message would tell every caller. When set, EngineLibraryModule::
    // InstallModules applies it to the real ScriptFunctionRegistry entry
    // via ScriptFunctionRegistry::MarkDeprecated right after the owning
    // module registers, so Call() (Native, Lua, and Visual Graph alike —
    // the one choke point every frontend shares) surfaces
    // FormatDeprecationWarning(canonicalName, *deprecation) in
    // ScriptFunctionCallResult::warnings on every real invocation.
    std::optional<LibraryDeprecation> deprecation;
};

// Cross-checks a LibraryFunctionDesc's recorded canonicalName/inputs/outputs
// against the real, live ScriptApiCatalog it claims to describe. Returns
// false if canonicalName does not resolve to a registered function, or if
// the recorded inputs/outputs differ in count, order, name, type, or
// required-ness from the catalog's real ScriptFunctionSignature-derived
// pins — proving the "wejścia, wyjścia" half of this type's contract is
// actually bound to the registry, not merely documentation that can drift
// silently out of sync with it.
[[nodiscard]] bool FunctionDescMatchesCatalog(const LibraryFunctionDesc& desc, const kb::script::ScriptApiCatalog& catalog);

} // namespace kb::library
