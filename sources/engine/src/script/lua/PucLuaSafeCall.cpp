#include "script/lua/LuaVm.hpp"
#include "engine/script/PucLuaSafeCall.hpp"

#include <exception>

namespace kb::script {

int PucLuaSafeRaise(lua_State* state, std::exception_ptr&& failure) {
    // The message goes on the Lua stack inside the handler; lua_error (a longjmp) runs only after
    // the handler has ended and released the exception, never from inside it.
    {
        const std::exception_ptr caught = std::move(failure);
        try {
            std::rethrow_exception(caught);
        } catch (const std::exception& exception) {
            luaL_where(state, 1);
            lua_pushstring(state, exception.what());
        } catch (...) {
            luaL_where(state, 1);
            lua_pushliteral(state, "kb::script: unknown C++ exception crossed a Lua-callable engine function");
        }
    }
    lua_concat(state, 2);
    return lua_error(state);
}

int PucLuaSafeInvoke(lua_State* state, PucLuaRawFunction function) {
    std::exception_ptr failure;
    try {
        return function(state);
    } catch (...) {
        failure = std::current_exception();
    }
    return PucLuaSafeRaise(state, std::move(failure));
}

} // namespace kb::script
