#include "engine/script/PucLuaSafeCall.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <exception>

namespace kb::script {

int PucLuaSafeInvoke(lua_State* state, PucLuaRawFunction function) {
    // The message goes on the Lua stack inside the handler; lua_error (a longjmp) runs only after
    // the handler has ended and released the exception, never from inside it.
    try {
        return function(state);
    } catch (const std::exception& exception) {
        luaL_where(state, 1);
        lua_pushstring(state, exception.what());
    } catch (...) {
        luaL_where(state, 1);
        lua_pushliteral(state, "kb::script: unknown C++ exception crossed a Lua-callable engine function");
    }
    lua_concat(state, 2);
    return lua_error(state);
}

} // namespace kb::script
