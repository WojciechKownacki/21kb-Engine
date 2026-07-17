#include "script/lua/api/PucLuaModuleApi.hpp"

#include "engine/script/PucLuaSafeCall.hpp"
#include "engine/script/PucLuaScriptRuntime.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <string>

namespace kb::script {
namespace {

[[nodiscard]] PucLuaScriptRuntime* RuntimeFromUpvalue(lua_State* state) noexcept {
    return static_cast<PucLuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
}

int LuaImport(lua_State* state) {
    PucLuaScriptRuntime* runtime = RuntimeFromUpvalue(state);
    if (runtime == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua runtime is not available");
        return 2;
    }
    const char* moduleName = luaL_checkstring(state, 1);
    std::string error;
    if (!runtime->PushModuleForImport(moduleName, error)) {
        lua_pushnil(state);
        lua_pushlstring(state, error.data(), error.size());
        return 2;
    }
    return 1;
}

} // namespace

void PucLuaModuleApi::AttachImport(lua_State* state, int environmentIndex, PucLuaScriptRuntime& runtime) {
    lua_pushlightuserdata(state, &runtime);
    lua_pushcclosure(state, &PucLuaSafeCall<&LuaImport>, 1);
    lua_setfield(state, environmentIndex, "Import");
}

} // namespace kb::script
