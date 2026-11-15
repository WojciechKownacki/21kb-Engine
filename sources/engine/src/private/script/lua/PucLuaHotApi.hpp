#pragma once

#include "script/lua/LuaVm.hpp"

namespace kb::script {

// The calls a script makes in every frame (reading and writing the position, an input action, the frame delta) reach
// the engine through LuaJIT's FFI, which compiled code can call; a call into the C API ends a compiled trace. Only the
// LuaJIT build has them; Lua 5.4 reaches the engine through the C API for every call.
class PucLuaHotApi final {
public:
    // Replaces those functions in the API tables of the script environment at `environmentIndex` (already attached).
    // A call with arguments the fast form does not cover is handed to the original function, which reports its errors.
#if defined(KB_SCRIPT_VM_LUAJIT)
    static void Attach(lua_State* state, int environmentIndex);
#else
    static void Attach(lua_State* /*state*/, int /*environmentIndex*/) noexcept {}
#endif
};

} // namespace kb::script
