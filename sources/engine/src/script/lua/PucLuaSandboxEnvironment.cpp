#include "script/lua/PucLuaSandboxEnvironment.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace kb::script {
namespace {

void CopyGlobalValueToEnvironment(lua_State* state, int environmentIndex, const char* name) {
    lua_getglobal(state, name);
    if (lua_istable(state, -1) != 0) {
        const int sourceIndex = lua_gettop(state);
        lua_newtable(state);
        const int copyIndex = lua_gettop(state);
        lua_pushnil(state);
        while (lua_next(state, sourceIndex) != 0) {
            lua_pushvalue(state, -2);
            lua_insert(state, -2);
            lua_settable(state, copyIndex);
        }
        lua_setfield(state, environmentIndex, name);
        lua_pop(state, 1);
        return;
    }
    lua_setfield(state, environmentIndex, name);
}

void Populate(lua_State* state, int environmentIndex) {
    static constexpr const char* kAllowedGlobals[] = {
        "assert",
        "error",
        "ipairs",
        "next",
        "pairs",
        "pcall",
        "print",
        "rawequal",
        "rawget",
        "rawlen",
        "rawset",
        "select",
        "tonumber",
        "tostring",
        "type",
        "xpcall",
        "coroutine",
        "table",
        "string",
        "math",
        "utf8",
    };
    for (const char* name : kAllowedGlobals) {
        CopyGlobalValueToEnvironment(state, environmentIndex, name);
    }
    lua_pushvalue(state, environmentIndex);
    lua_setfield(state, environmentIndex, "_G");
}

} // namespace

void PucLuaSandboxEnvironment::OpenSafeLibraries(lua_State* state) {
    luaL_requiref(state, "_G", luaopen_base, 1);
    lua_pop(state, 1);
    lua_pushnil(state);
    lua_setglobal(state, "dofile");
    lua_pushnil(state);
    lua_setglobal(state, "loadfile");
    lua_pushnil(state);
    lua_setglobal(state, "load");
    lua_pushnil(state);
    lua_setglobal(state, "collectgarbage");
    lua_pushnil(state);
    lua_setglobal(state, "require");
    lua_pushnil(state);
    lua_setglobal(state, "package");
    luaL_requiref(state, "coroutine", luaopen_coroutine, 1);
    lua_pop(state, 1);
    luaL_requiref(state, "table", luaopen_table, 1);
    lua_pop(state, 1);
    luaL_requiref(state, "string", luaopen_string, 1);
    lua_pop(state, 1);
    luaL_requiref(state, "math", luaopen_math, 1);
    lua_pop(state, 1);
    luaL_requiref(state, "utf8", luaopen_utf8, 1);
    lua_pop(state, 1);
}

void PucLuaSandboxEnvironment::Create(lua_State* state) {
    lua_newtable(state);
    Populate(state, lua_gettop(state));
}

} // namespace kb::script
