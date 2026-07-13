#pragma once

#include <string>
#include <string_view>

namespace kb::library {

// A uniform description of why a kb::library operation failed: what was
// attempted (operation) and why (message). Deliberately lighter than
// kb::script::ScriptDiagnostic (no entity/asset/backend): library-level
// helpers (EntityHandle, future domain modules) do not always know which
// script/entity/asset triggered the call. The ScriptFunctionRegistry
// boundary, which does have that context, is responsible for folding a
// caught exception or a ScriptError into a full ScriptDiagnostic — see the
// catch blocks in ScriptFunctionRegistry::Call and
// NativeScriptBackend::ExecuteLifecycle/ExecuteEvent.
struct ScriptError {
    std::string operation;
    std::string message;
};

[[nodiscard]] std::string ToString(const ScriptError& error);

} // namespace kb::library
