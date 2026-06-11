#include "script/lua/api/PucLuaFunctionApi.hpp"

#include "engine/script/ScriptExecutionContext.hpp"
#include "engine/script/ScriptValue.hpp"
#include "script/lua/PucLuaValueBridge.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace kb::script {
namespace {

[[nodiscard]] ScriptExecutionContext* ContextFromUpvalue(lua_State* state) noexcept {
    return static_cast<ScriptExecutionContext*>(lua_touserdata(state, lua_upvalueindex(1)));
}

[[nodiscard]] std::vector<ScriptFunctionArgument> ArgumentsFromTable(lua_State* state, int index) {
    std::vector<ScriptFunctionArgument> arguments;
    if (lua_istable(state, index) == 0) {
        return arguments;
    }

    const int absoluteIndex = lua_absindex(state, index);
    lua_pushnil(state);
    while (lua_next(state, absoluteIndex) != 0) {
        if (lua_type(state, -2) == LUA_TSTRING) {
            arguments.push_back(ScriptFunctionArgument{
                .name = lua_tostring(state, -2),
                .value = PucLuaValueBridge::FromLua(state, -1),
            });
        }
        lua_pop(state, 1);
    }
    return arguments;
}

int LuaCallFunction(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    const char* functionName = luaL_checkstring(state, 1);
    std::vector<ScriptFunctionArgument> arguments;
    if (lua_gettop(state) >= 2 && lua_istable(state, 2) != 0) {
        arguments = ArgumentsFromTable(state, 2);
    }
    const ScriptFunctionCallResult result = context->CallFunction(functionName, arguments);
    if (!result.Succeeded()) {
        lua_pushnil(state);
        const std::string error = result.errors.empty() ? "script function call failed" : result.errors.front();
        lua_pushlstring(state, error.data(), error.size());
        return 2;
    }
    if (result.outputs.empty()) {
        lua_pushnil(state);
        return 1;
    }
    if (result.outputs.size() == 1U) {
        PucLuaValueBridge::Push(state, result.outputs.front().value);
        return 1;
    }
    lua_createtable(state, 0, static_cast<int>(result.outputs.size()));
    for (const ScriptFunctionArgument& output : result.outputs) {
        PucLuaValueBridge::Push(state, output.value);
        lua_setfield(state, -2, output.name.c_str());
    }
    return 1;
}

// Log(value): print-style helper. Coerces any value to a string and forwards it
// to the registered "Log" function (the editor routes that to its Console). A
// no-op when no "Log" function is registered (e.g. headless engine hosts).
int LuaLog(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        return 0;
    }
    std::size_t length = 0;
    const char* text = luaL_tolstring(state, 1, &length);
    std::vector<ScriptFunctionArgument> arguments;
    arguments.push_back(ScriptFunctionArgument{
        .name = "message",
        .value = ScriptValue{ std::string{ text != nullptr ? text : "", text != nullptr ? length : std::size_t{ 0 } } },
    });
    if (text != nullptr) {
        lua_pop(state, 1);
    }
    static_cast<void>(context->CallFunction("Log", arguments));
    return 0;
}

int LuaAudioPlay(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }

    std::vector<ScriptFunctionArgument> arguments;
    if (lua_istable(state, 1) != 0) {
        arguments = ArgumentsFromTable(state, 1);
    } else {
        std::size_t length = 0;
        const char* clip = luaL_tolstring(state, 1, &length);
        arguments.push_back(ScriptFunctionArgument{
            .name = "clip",
            .value = ScriptValue{ std::string{ clip != nullptr ? clip : "", clip != nullptr ? length : std::size_t{ 0 } } },
        });
        if (clip != nullptr) {
            lua_pop(state, 1);
        }
        if (lua_gettop(state) >= 2 && lua_istable(state, 2) != 0) {
            std::vector<ScriptFunctionArgument> options = ArgumentsFromTable(state, 2);
            arguments.insert(arguments.end(), options.begin(), options.end());
        }
    }

    const ScriptFunctionCallResult result = context->CallFunction("Audio.Play", arguments);
    if (!result.Succeeded()) {
        lua_pushnil(state);
        const std::string error = result.errors.empty() ? "audio play failed" : result.errors.front();
        lua_pushlstring(state, error.data(), error.size());
        return 2;
    }

    const std::optional<ScriptValue> voice = result.Output("voice");
    if (!voice.has_value()) {
        lua_pushnil(state);
        return 1;
    }
    PucLuaValueBridge::Push(state, *voice);
    return 1;
}

} // namespace

void PucLuaFunctionApi::Attach(lua_State* state, int environmentIndex, ScriptExecutionContext& context) {
    lua_pushlightuserdata(state, &context);
    lua_pushcclosure(state, &LuaCallFunction, 1);
    lua_setfield(state, environmentIndex, "CallFunction");

    lua_pushlightuserdata(state, &context);
    lua_pushcclosure(state, &LuaLog, 1);
    lua_setfield(state, environmentIndex, "Log");

    lua_createtable(state, 0, 1);
    lua_pushlightuserdata(state, &context);
    lua_pushcclosure(state, &LuaAudioPlay, 1);
    lua_setfield(state, -2, "Play");
    lua_setfield(state, environmentIndex, "Audio");
}

} // namespace kb::script
