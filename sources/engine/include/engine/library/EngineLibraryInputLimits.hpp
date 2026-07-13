#pragma once

#include <cstddef>

namespace kb::library {

// A single, documented set of input limits shared across kb::library
// rather than each module inventing its own bound:
//   - maxStringLength: the largest a single String ScriptValue argument may
//     be, enforced today by kb::script::ScriptFunctionRegistry::
//     ValidateInputs for every registered function (Native, Lua,
//     Visual Graph all funnel through it) — this value must match the
//     kMaxScriptStringArgumentLength constant enforced there.
//   - maxGraphRecursionDepth: the deepest a chain of Emit-triggered event
//     handlers may recurse before ScriptRuntime::DrainEvents gives up and
//     reports a diagnostic instead of looping forever — matches
//     kb::script::ScriptRuntimeDispatchOptions::maxEventDepth's default,
//     already enforced today.
//   - maxCollectionSize, maxEventPayloadArguments: reserved policy values.
//     kb::library has no collection type to bound yet (LIB-058 introduces
//     Array<T>/Map<K,V>); nothing enforces these two today. They exist so
//     LIB-058 and future event-payload work start from an agreed number
//     instead of each picking their own.
struct LibraryInputLimits {
    std::size_t maxStringLength = 65536U;
    std::size_t maxCollectionSize = 4096U;
    std::size_t maxEventPayloadArguments = 32U;
    std::size_t maxGraphRecursionDepth = 8U;
};

inline constexpr LibraryInputLimits kDefaultLibraryInputLimits{};

} // namespace kb::library
