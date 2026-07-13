#pragma once

#include <cstdint>
#include <string>

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
// ScriptFunctionRegistry entry, and inputs/outputs stay defined in exactly
// one place. canFail describes whether the function can report a runtime
// ScriptError (LIB-010) beyond input-validation errors the registry itself
// already rejects (unknown function, type mismatch, missing required pin).
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
};

} // namespace kb::library
