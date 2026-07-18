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
//   - maxCollectionSize: the largest capacity any kb::library collection
//     (Array/Set/Map/Queue/Stack — EngineLibraryCollections.hpp) will honor;
//     every collection constructor clamps a larger request down to this
//     value (LIB-058), so a script cannot grow an unbounded collection by
//     asking for more per call.
//   - maxEventPayloadArguments: the largest number of arguments a single
//     ScriptEvent may carry, enforced at every event dispatch entry point
//     (ScriptEventBus::Emit/EmitDeferred and ScriptRuntime::DispatchEvent)
//     via the duplicated kb::script::kMaxScriptEventArguments constant — kb::
//     script must never depend on kb::library, so the value is mirrored there
//     and kept in lockstep by a consistency assertion in RunInputLimitsTest.
struct LibraryInputLimits {
    std::size_t maxStringLength = 65536U;
    std::size_t maxCollectionSize = 4096U;
    std::size_t maxEventPayloadArguments = 32U;
    std::size_t maxGraphRecursionDepth = 8U;
};

inline constexpr LibraryInputLimits kDefaultLibraryInputLimits{};

} // namespace kb::library
