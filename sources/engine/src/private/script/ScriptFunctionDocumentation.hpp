#pragma once

#include <string_view>

namespace kb::script {

// Returns the authored documentation for an engine-owned script function.
// An empty result means the function is not part of the built-in API.
[[nodiscard]] std::string_view BuiltInScriptFunctionDescription(std::string_view canonicalName) noexcept;

} // namespace kb::script
