#pragma once

#include "script/lua/LuaVm.hpp"

namespace kb::script {

// The `bit` library LuaJIT has, for the Lua 5.4 build (see vm/PucLuaBitLibrary.cpp). The LuaJIT build uses its own.
class PucLuaBitLibrary final {
public:
    static int Open(lua_State* state);
};

} // namespace kb::script
