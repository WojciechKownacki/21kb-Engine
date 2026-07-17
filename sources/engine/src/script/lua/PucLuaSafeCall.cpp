#include "engine/script/PucLuaSafeCall.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <exception>

namespace kb::script {

int PucLuaSafeInvoke(lua_State* state, PucLuaRawFunction function) {
    try {
        return function(state);
    } catch (const std::exception& exception) {
        return luaL_error(state, "%s", exception.what());
    } catch (...) {
        return luaL_error(state, "kb::script: unknown C++ exception crossed a Lua-callable engine function");
    }
}

} // namespace kb::script
