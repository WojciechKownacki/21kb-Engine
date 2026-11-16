#pragma once

#include "script/lua/LuaVm.hpp"

#include <string>
#include <vector>

namespace kb::script {

class ScriptExecutionContext;
class ScriptFunctionRegistry;

// The calls a script makes in every frame (reading and writing the transform, the input actions, the frame delta, the
// common physics queries) reach the engine through LuaJIT's FFI, which compiled code can call; a call into the C API ends a
// compiled trace. Only the LuaJIT build has them; Lua 5.4 reaches the engine through the C API for every call.
class PucLuaHotApi final {
public:
    // Replaces those functions in the API tables of the script environment at `environmentIndex` (already attached). The
    // argument checks and the result shape of each replacement come from the function's entry in the API catalog and its
    // registered pins. A call with arguments the fast form does not cover is handed to the original function, which reports
    // its errors.
#if defined(KB_SCRIPT_VM_LUAJIT)
    static void Attach(lua_State* state, int environmentIndex, ScriptExecutionContext& context);
    // What keeps a fast call from being installed against `functions`: a binding that is not in the API catalog, whose C
    // signature differs from its pins, or whose generated Lua does not load. Empty when every one can be installed.
    [[nodiscard]] static std::vector<std::string> Verify(const ScriptFunctionRegistry& functions);
#else
    static void Attach(lua_State* /*state*/, int /*environmentIndex*/, ScriptExecutionContext& /*context*/) noexcept {}
    [[nodiscard]] static std::vector<std::string> Verify(const ScriptFunctionRegistry& /*functions*/) { return {}; }
#endif
};

} // namespace kb::script
