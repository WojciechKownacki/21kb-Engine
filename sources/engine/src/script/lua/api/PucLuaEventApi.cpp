#include "script/lua/api/PucLuaEventApi.hpp"

#include "engine/script/ScriptExecutionContext.hpp"
#include "script/lua/PucLuaValueBridge.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <cstdint>
#include <utility>
#include <vector>

namespace kb::script {
namespace {

[[nodiscard]] ScriptExecutionContext* ContextFromUpvalue(lua_State* state) noexcept {
    return static_cast<ScriptExecutionContext*>(lua_touserdata(state, lua_upvalueindex(1)));
}

int LuaEmit(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        return 0;
    }
    const char* eventName = luaL_checkstring(state, 1);
    std::vector<ScriptEventArgument> arguments;
    if (lua_gettop(state) >= 2 && lua_istable(state, 2) != 0) {
        arguments = PucLuaEventApi::ArgumentsFromTable(state, 2);
    }
    context->Emit(eventName, std::move(arguments));
    return 0;
}

int LuaEmitTo(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        return 0;
    }
    const lua_Integer targetId = luaL_checkinteger(state, 1);
    if (targetId < 0) {
        return luaL_argerror(state, 1, "target entity id must be non-negative");
    }
    const auto target = kb::scene::SceneEntity{ static_cast<std::uint64_t>(targetId) };
    const char* eventName = luaL_checkstring(state, 2);
    std::vector<ScriptEventArgument> arguments;
    if (lua_gettop(state) >= 3 && lua_istable(state, 3) != 0) {
        arguments = PucLuaEventApi::ArgumentsFromTable(state, 3);
    }
    context->EmitTo(target, eventName, std::move(arguments));
    return 0;
}

} // namespace

void PucLuaEventApi::Attach(lua_State* state, int environmentIndex, ScriptExecutionContext& context) {
    lua_pushlightuserdata(state, &context);
    lua_pushcclosure(state, &LuaEmit, 1);
    lua_setfield(state, environmentIndex, "Emit");
    lua_pushlightuserdata(state, &context);
    lua_pushcclosure(state, &LuaEmitTo, 1);
    lua_setfield(state, environmentIndex, "EmitTo");
}

std::vector<ScriptEventArgument> PucLuaEventApi::ArgumentsFromTable(lua_State* state, int index) {
    std::vector<ScriptEventArgument> arguments;
    if (lua_istable(state, index) == 0) {
        return arguments;
    }

    const int absoluteIndex = lua_absindex(state, index);
    lua_pushnil(state);
    while (lua_next(state, absoluteIndex) != 0) {
        if (lua_type(state, -2) == LUA_TSTRING) {
            arguments.push_back(ScriptEventArgument{
                .name = lua_tostring(state, -2),
                .value = PucLuaValueBridge::FromLua(state, -1),
            });
        }
        lua_pop(state, 1);
    }
    return arguments;
}

void PucLuaEventApi::PushEvent(lua_State* state, const ScriptEvent& event) {
    lua_createtable(state, 0, 4);
    lua_pushlstring(state, event.name.data(), event.name.size());
    lua_setfield(state, -2, "name");
    lua_pushinteger(state, static_cast<lua_Integer>(event.sender.Id()));
    lua_setfield(state, -2, "sender");
    lua_pushinteger(state, static_cast<lua_Integer>(event.target.Id()));
    lua_setfield(state, -2, "target");
    lua_pushinteger(state, static_cast<lua_Integer>(event.senderAsset.value));
    lua_setfield(state, -2, "senderAsset");
    lua_createtable(state, 0, static_cast<int>(event.arguments.size()));
    for (const ScriptEventArgument& argument : event.arguments) {
        PucLuaValueBridge::Push(state, argument.value);
        lua_setfield(state, -2, argument.name.c_str());
    }
    lua_setfield(state, -2, "args");
}

} // namespace kb::script
