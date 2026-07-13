#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace kb::library {

// The closed set of reasons a kb::library operation can fail. Every
// distinct failure kb::library needs to distinguish today maps to exactly
// one of these; a new failure kind that doesn't fit is a reason to extend
// this enum, not to overload an existing value's meaning:
//   - InvalidHandle: a handle (EntityHandle, ...) is structurally invalid,
//     stale (its entity was destroyed), or belongs to a different
//     world/scene than the one it was checked against.
//   - InactiveWorld: the world/scene itself is not in a state that accepts
//     the operation (e.g. not currently running).
//   - UnavailableCapability: the operation targets a module whose
//     capability is false (LIB-016/027) — its backend was never compiled
//     into this build, not a runtime fault.
//   - Permission: the caller is not allowed to perform the operation
//     (thread affinity, lifecycle phase, or authority restriction).
//   - InvalidArgument: an argument's value or type does not satisfy the
//     operation's contract.
//   - Timeout: the operation did not complete within its allotted budget.
enum class LibraryErrorCode : std::uint8_t {
    InvalidHandle,
    InactiveWorld,
    UnavailableCapability,
    Permission,
    InvalidArgument,
    Timeout,
};

[[nodiscard]] const char* ToString(LibraryErrorCode code) noexcept;

// A uniform description of why a kb::library operation failed: which of
// the LibraryErrorCode categories it falls into, what was attempted
// (operation), and why in human-readable terms (message). Deliberately
// lighter than kb::script::ScriptDiagnostic (no entity/asset/backend):
// library-level helpers (EntityHandle, future domain modules) do not
// always know which script/entity/asset triggered the call. The
// ScriptFunctionRegistry boundary, which does have that context, is
// responsible for folding a caught exception or a ScriptError into a full
// ScriptDiagnostic — see the catch blocks in ScriptFunctionRegistry::Call
// and NativeScriptBackend::ExecuteLifecycle/ExecuteEvent.
struct ScriptError {
    LibraryErrorCode code = LibraryErrorCode::InvalidArgument;
    std::string operation;
    std::string message;
};

[[nodiscard]] std::string ToString(const ScriptError& error);

} // namespace kb::library
