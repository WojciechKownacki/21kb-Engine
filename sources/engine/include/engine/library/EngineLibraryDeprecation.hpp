#pragma once

#include "engine/library/EngineLibrary.hpp"
#include "engine/visual/VisualGraphTypes.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace kb::library {

// Declares a registered kb::library function as deprecated: why
// (message), what to use instead (replacementCanonicalName — empty if
// there is no direct replacement, e.g. the capability was removed
// outright), and which LibraryApiVersion first deprecated it. Attached to
// LibraryFunctionDesc (LIB-017) as an optional field so a function's
// deprecation status lives with its other audited metadata rather than in
// a separate, driftable table.
struct LibraryDeprecation {
    std::string message;
    std::string replacementCanonicalName;
    LibraryApiVersion sinceVersion{};
};

// The message to surface to a Lua script or C++ caller of a deprecated
// function: what's deprecated, why, since when, and what to call instead
// if there is a replacement. This is a diagnostic string, not itself a
// hard error — a deprecated function still runs; something upstream (a log
// call, a script diagnostic) decides how loudly to surface it.
[[nodiscard]] std::string FormatDeprecationWarning(std::string_view functionName, const LibraryDeprecation& deprecation);

// Rewrites every Visual Graph CallNative node in `nodes` bound to the
// deprecated function (symbol == "Function." + deprecatedCanonicalName,
// the auto-generated binding key ScriptFunctionVisualGraphBindings gives
// every registered function) to reference deprecation.replacementCanonicalName
// instead. Returns the number of nodes migrated. A deprecation declared
// without a replacement (replacementCanonicalName empty) migrates nothing —
// silently rewriting a removed capability to some other function would
// change graph behavior instead of flagging it for a human to resolve.
[[nodiscard]] std::size_t MigrateVisualGraphCallNativeNodes(
    std::vector<kb::visual::VisualGraphNode>& nodes,
    std::string_view deprecatedCanonicalName,
    const LibraryDeprecation& deprecation);

} // namespace kb::library
