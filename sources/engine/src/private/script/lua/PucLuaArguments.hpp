#pragma once

#include "script/lua/LuaVm.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>

namespace kb::script {

// The luaL_check*/luaL_opt*/luaL_error family raises with longjmp, which skips (or, on MSVC,
// half-runs) the destructors of C++ objects alive in the calling frame - a bad argument from a
// script then corrupts the stack instead of failing the call. These report the same errors as a
// C++ exception, which unwinds the frame properly; PucLuaSafeInvoke turns it into the Lua error.
[[noreturn]] inline void PucLuaRaise(const std::string& message) {
    throw std::runtime_error(message);
}

[[noreturn]] inline void PucLuaArgumentError(lua_State* state, int argument, const char* message) {
    PucLuaRaise("bad argument #" + std::to_string(argument) + " (" + message + ", got " + luaL_typename(state, argument) + ")");
}

inline void PucLuaArgumentCheck(lua_State* /*state*/, bool condition, int argument, const char* message) {
    if (!condition) PucLuaRaise("bad argument #" + std::to_string(argument) + " (" + message + ")");
}

inline void PucLuaCheckType(lua_State* state, int argument, int type) {
    if (lua_type(state, argument) != type) PucLuaArgumentError(state, argument, (std::string{ lua_typename(state, type) } + " expected").c_str());
}

[[nodiscard]] inline lua_Number PucLuaCheckNumber(lua_State* state, int argument) {
    int isNumber = 0;
    const lua_Number value = lua_tonumberx(state, argument, &isNumber);
    if (isNumber == 0) PucLuaArgumentError(state, argument, "number expected");
    return value;
}

[[nodiscard]] inline lua_Integer PucLuaCheckInteger(lua_State* state, int argument) {
    int isInteger = 0;
    const lua_Integer value = lua_tointegerx(state, argument, &isInteger);
    if (isInteger == 0) {
        PucLuaArgumentError(state, argument, lua_isnumber(state, argument) != 0 ? "number has no integer representation" : "integer expected");
    }
    return value;
}

[[nodiscard]] inline const char* PucLuaCheckLString(lua_State* state, int argument, std::size_t* length) {
    const char* value = lua_type(state, argument) == LUA_TSTRING || lua_type(state, argument) == LUA_TNUMBER
        ? lua_tolstring(state, argument, length) : nullptr;
    if (value == nullptr) PucLuaArgumentError(state, argument, "string expected");
    return value;
}

[[nodiscard]] inline const char* PucLuaCheckString(lua_State* state, int argument) {
    return PucLuaCheckLString(state, argument, nullptr);
}

[[nodiscard]] inline lua_Number PucLuaOptNumber(lua_State* state, int argument, lua_Number fallback) {
    return lua_isnoneornil(state, argument) != 0 ? fallback : PucLuaCheckNumber(state, argument);
}

[[nodiscard]] inline lua_Integer PucLuaOptInteger(lua_State* state, int argument, lua_Integer fallback) {
    return lua_isnoneornil(state, argument) != 0 ? fallback : PucLuaCheckInteger(state, argument);
}

} // namespace kb::script
