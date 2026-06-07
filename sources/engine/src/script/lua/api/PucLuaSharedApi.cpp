#include "script/lua/api/PucLuaSharedApi.hpp"

#include "engine/script/ScriptExecutionContext.hpp"
#include "script/lua/PucLuaValueBridge.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <optional>

namespace kb::script {
namespace {

[[nodiscard]] ScriptExecutionContext* ContextFromUpvalue(lua_State* state) noexcept {
    return static_cast<ScriptExecutionContext*>(lua_touserdata(state, lua_upvalueindex(1)));
}

int LuaSetShared(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const char* key = luaL_checkstring(state, 1);
    lua_pushboolean(state, context->SetSharedValue(key, PucLuaValueBridge::FromLua(state, 2)) ? 1 : 0);
    return 1;
}

int LuaGetShared(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    const char* key = luaL_checkstring(state, 1);
    const std::optional<ScriptValue> value = context->GetSharedValue(key);
    if (!value.has_value()) {
        lua_pushnil(state);
        return 1;
    }
    PucLuaValueBridge::Push(state, *value);
    return 1;
}

int LuaHasShared(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const char* key = luaL_checkstring(state, 1);
    lua_pushboolean(state, context->HasSharedValue(key) ? 1 : 0);
    return 1;
}

int LuaRemoveShared(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const char* key = luaL_checkstring(state, 1);
    lua_pushboolean(state, context->RemoveSharedValue(key) ? 1 : 0);
    return 1;
}

} // namespace

void PucLuaSharedApi::Attach(lua_State* state, int environmentIndex, ScriptExecutionContext& context) {
    lua_pushlightuserdata(state, &context);
    lua_pushcclosure(state, &LuaSetShared, 1);
    lua_setfield(state, environmentIndex, "SetShared");
    lua_pushlightuserdata(state, &context);
    lua_pushcclosure(state, &LuaGetShared, 1);
    lua_setfield(state, environmentIndex, "GetShared");
    lua_pushlightuserdata(state, &context);
    lua_pushcclosure(state, &LuaHasShared, 1);
    lua_setfield(state, environmentIndex, "HasShared");
    lua_pushlightuserdata(state, &context);
    lua_pushcclosure(state, &LuaRemoveShared, 1);
    lua_setfield(state, environmentIndex, "RemoveShared");
}

} // namespace kb::script
