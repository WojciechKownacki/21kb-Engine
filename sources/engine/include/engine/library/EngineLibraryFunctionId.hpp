#pragma once

#include <cstdint>
#include <string_view>

namespace kb::library {

using LibraryFunctionId = std::uint64_t;

// A deterministic hash of a function's canonical name (LIB-017's
// LibraryFunctionDesc::canonicalName / kb::script::ScriptFunctionSignature
// ::name) — stable across sessions, builds, and registration order, unlike
// an index into ScriptFunctionRegistry::Functions() (which shifts if a
// module registers its functions in a different order, or a module is
// added/removed). Two functions with the same canonical name always
// produce the same id, and the id does not change if unrelated functions
// are registered before or after it.
//
// This does not replace name-based lookup — ScriptFunctionRegistry::Call
// still resolves by name, and nothing in the runtime looks a function up
// by this id today. It exists for future callers that need a compact,
// order-independent key instead of comparing full strings (e.g. a Visual
// Graph binary IR, or LIB-235's hot-path binding cache).
[[nodiscard]] LibraryFunctionId ComputeLibraryFunctionId(std::string_view canonicalName) noexcept;

} // namespace kb::library
