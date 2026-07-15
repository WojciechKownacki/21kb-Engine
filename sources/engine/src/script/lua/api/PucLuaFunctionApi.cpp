#include "script/lua/api/PucLuaFunctionApi.hpp"

#include "engine/scene/PhysicsBackend.hpp"
#include "engine/script/ScriptExecutionContext.hpp"
#include "engine/script/ScriptValue.hpp"
#include "script/lua/PucLuaValueBridge.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
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

[[nodiscard]] int PushCallError(lua_State* state, const ScriptFunctionCallResult& result, std::string_view fallback) {
    lua_pushnil(state);
    const std::string error = result.errors.empty() ? std::string{ fallback } : result.errors.front();
    lua_pushlstring(state, error.data(), error.size());
    return 2;
}

[[nodiscard]] ScriptFunctionArgument Arg(std::string name, ScriptValue value) {
    return ScriptFunctionArgument{ std::move(name), std::move(value) };
}

int LuaWorldFindByName(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    const char* name = luaL_checkstring(state, 1);
    std::vector<ScriptFunctionArgument> arguments{ Arg("name", ScriptValue{ std::string{ name != nullptr ? name : "" } }) };
    const ScriptFunctionCallResult result = context->CallFunction("World.FindByName", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "world find failed");
    }
    PucLuaValueBridge::Push(state, result.Output("entity").value_or(ScriptValue{ 0U, ScriptValueType::Entity }));
    return 1;
}

int LuaWorldExists(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const auto entity = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    std::vector<ScriptFunctionArgument> arguments{ Arg("entity", ScriptValue{ entity, ScriptValueType::Entity }) };
    const ScriptFunctionCallResult result = context->CallFunction("World.Exists", arguments);
    lua_pushboolean(state, result.Output("exists").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaWorldName(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushliteral(state, "");
        return 1;
    }
    const auto entity = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    std::vector<ScriptFunctionArgument> arguments{ Arg("entity", ScriptValue{ entity, ScriptValueType::Entity }) };
    const ScriptFunctionCallResult result = context->CallFunction("World.Name", arguments);
    const std::string name = result.Output("name").value_or(ScriptValue{ std::string{} }).AsString();
    lua_pushlstring(state, name.data(), name.size());
    return 1;
}

int LuaWorldSpawn(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    std::vector<ScriptFunctionArgument> arguments;
    if (lua_istable(state, 1) != 0) {
        arguments = ArgumentsFromTable(state, 1);
    } else if (lua_gettop(state) >= 1 && lua_isnil(state, 1) == 0) {
        std::size_t length = 0;
        const char* name = luaL_tolstring(state, 1, &length);
        arguments.push_back(Arg("name", ScriptValue{ std::string{ name != nullptr ? name : "", name != nullptr ? length : std::size_t{ 0 } } }));
        if (name != nullptr) {
            lua_pop(state, 1);
        }
    }
    const ScriptFunctionCallResult result = context->CallFunction("World.Spawn", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "world spawn failed");
    }
    PucLuaValueBridge::Push(state, result.Output("entity").value_or(ScriptValue{ 0U, ScriptValueType::Entity }));
    return 1;
}

int LuaWorldDestroy(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const auto entity = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    std::vector<ScriptFunctionArgument> arguments{ Arg("entity", ScriptValue{ entity, ScriptValueType::Entity }) };
    const ScriptFunctionCallResult result = context->CallFunction("World.Destroy", arguments);
    lua_pushboolean(state, result.Output("destroyed").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaWorldSetTag(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const auto entity = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    const char* tag = luaL_checkstring(state, 2);
    const bool enabled = lua_gettop(state) < 3 || lua_toboolean(state, 3) != 0;
    std::vector<ScriptFunctionArgument> arguments{
        Arg("entity", ScriptValue{ entity, ScriptValueType::Entity }),
        Arg("tag", ScriptValue{ std::string{ tag != nullptr ? tag : "" } }),
        Arg("enabled", ScriptValue{ enabled }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("World.SetTag", arguments);
    lua_pushboolean(state, result.Output("tagged").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaWorldHasTag(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const auto entity = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    const char* tag = luaL_checkstring(state, 2);
    std::vector<ScriptFunctionArgument> arguments{
        Arg("entity", ScriptValue{ entity, ScriptValueType::Entity }),
        Arg("tag", ScriptValue{ std::string{ tag != nullptr ? tag : "" } }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("World.HasTag", arguments);
    lua_pushboolean(state, result.Output("tagged").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaWorldFindByTag(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    const char* tag = luaL_checkstring(state, 1);
    std::vector<ScriptFunctionArgument> arguments{ Arg("tag", ScriptValue{ std::string{ tag != nullptr ? tag : "" } }) };
    const ScriptFunctionCallResult result = context->CallFunction("World.FindByTag", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "world find by tag failed");
    }
    PucLuaValueBridge::Push(state, result.Output("entity").value_or(ScriptValue{ 0U, ScriptValueType::Entity }));
    return 1;
}

int LuaWorldSetParent(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const auto entity = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    const auto parent = static_cast<std::uint64_t>(luaL_optinteger(state, 2, 0));
    std::vector<ScriptFunctionArgument> arguments{
        Arg("entity", ScriptValue{ entity, ScriptValueType::Entity }),
        Arg("parent", ScriptValue{ parent, ScriptValueType::Entity }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("World.SetParent", arguments);
    lua_pushboolean(state, result.Output("parented").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaWorldInstantiatePrefab(lua_State* state) {
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
        const char* prefab = luaL_checkstring(state, 1);
        arguments.push_back(Arg("prefab", ScriptValue{ std::string{ prefab != nullptr ? prefab : "" } }));
    }
    const ScriptFunctionCallResult result = context->CallFunction("World.InstantiatePrefab", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "prefab instantiate failed");
    }
    PucLuaValueBridge::Push(state, result.Output("entity").value_or(ScriptValue{ 0U, ScriptValueType::Entity }));
    return 1;
}

int LuaTransformGetPosition(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    const auto entity = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    std::vector<ScriptFunctionArgument> arguments{ Arg("entity", ScriptValue{ entity, ScriptValueType::Entity }) };
    const ScriptFunctionCallResult result = context->CallFunction("Transform.GetPosition", arguments);
    if (!result.Succeeded() || !result.Output("found").value_or(ScriptValue{ false }).AsBool()) {
        lua_pushnil(state);
        return 1;
    }

    lua_createtable(state, 0, 3);
    lua_pushnumber(state, static_cast<lua_Number>(result.Output("x").value_or(ScriptValue{ 0.0F }).AsFloat()));
    lua_setfield(state, -2, "x");
    lua_pushnumber(state, static_cast<lua_Number>(result.Output("y").value_or(ScriptValue{ 0.0F }).AsFloat()));
    lua_setfield(state, -2, "y");
    lua_pushnumber(state, static_cast<lua_Number>(result.Output("z").value_or(ScriptValue{ 0.0F }).AsFloat()));
    lua_setfield(state, -2, "z");
    return 1;
}

int LuaTransformSetPosition(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    std::vector<ScriptFunctionArgument> arguments;
    if (lua_istable(state, 1) != 0) {
        arguments = ArgumentsFromTable(state, 1);
    } else {
        arguments = {
            Arg("entity", ScriptValue{ static_cast<std::uint64_t>(luaL_checkinteger(state, 1)), ScriptValueType::Entity }),
            Arg("x", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
            Arg("y", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
            Arg("z", ScriptValue{ static_cast<float>(luaL_checknumber(state, 4)) }),
        };
    }
    const ScriptFunctionCallResult result = context->CallFunction("Transform.SetPosition", arguments);
    lua_pushboolean(state, result.Output("moved").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaTransformTranslate(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    std::vector<ScriptFunctionArgument> arguments;
    if (lua_istable(state, 1) != 0) {
        arguments = ArgumentsFromTable(state, 1);
    } else {
        arguments = {
            Arg("entity", ScriptValue{ static_cast<std::uint64_t>(luaL_checkinteger(state, 1)), ScriptValueType::Entity }),
            Arg("x", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
            Arg("y", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
            Arg("z", ScriptValue{ static_cast<float>(luaL_checknumber(state, 4)) }),
        };
    }
    const ScriptFunctionCallResult result = context->CallFunction("Transform.Translate", arguments);
    lua_pushboolean(state, result.Output("moved").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaTimeDelta(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnumber(state, 0.0);
        return 1;
    }
    const ScriptFunctionCallResult result = context->CallFunction("Time.Delta", {});
    lua_pushnumber(state, static_cast<lua_Number>(result.Output("delta").value_or(ScriptValue{ 0.0F }).AsFloat()));
    return 1;
}

int LuaPhysicsRaycast(lua_State* state) {
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
        arguments = {
            Arg("originX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 1)) }),
            Arg("originY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
            Arg("originZ", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
            Arg("directionX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 4)) }),
            Arg("directionY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 5)) }),
            Arg("directionZ", ScriptValue{ static_cast<float>(luaL_checknumber(state, 6)) }),
            Arg("distance", ScriptValue{ static_cast<float>(luaL_optnumber(state, 7, 1000.0)) }),
        };
    }
    const ScriptFunctionCallResult result = context->CallFunction("Physics.Raycast", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "physics raycast failed");
    }
    lua_createtable(state, 0, 9);
    for (const ScriptFunctionArgument& output : result.outputs) {
        PucLuaValueBridge::Push(state, output.value);
        lua_setfield(state, -2, output.name.c_str());
    }
    return 1;
}

// LIB-124: entity is ALWAYS read via luaL_checkinteger and explicitly tagged
// ScriptValueType::Entity here - NOT through the generic table/
// ArgumentsFromTable path other wrappers above also support, which infers a
// Lua integer's ScriptValueType purely from its own magnitude
// (PucLuaValueBridge::FromLua: only becomes Entity-typed once the value
// exceeds int32 range) and would silently mis-marshal a small-magnitude
// entity id as Int instead (see LIB-123's World.SetPropertyEntity notes).
[[nodiscard]] std::uint64_t CheckEntityArg(lua_State* state, int index) {
    return static_cast<std::uint64_t>(luaL_checkinteger(state, index));
}

int LuaPhysicsAddForce(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("entity", ScriptValue{ CheckEntityArg(state, 1), ScriptValueType::Entity }),
        Arg("forceX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
        Arg("forceY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
        Arg("forceZ", ScriptValue{ static_cast<float>(luaL_checknumber(state, 4)) }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("Physics.AddForce", arguments);
    lua_pushboolean(state, result.Output("applied").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaPhysicsAddImpulse(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("entity", ScriptValue{ CheckEntityArg(state, 1), ScriptValueType::Entity }),
        Arg("impulseX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
        Arg("impulseY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
        Arg("impulseZ", ScriptValue{ static_cast<float>(luaL_checknumber(state, 4)) }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("Physics.AddImpulse", arguments);
    lua_pushboolean(state, result.Output("applied").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaPhysicsSetVelocity(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("entity", ScriptValue{ CheckEntityArg(state, 1), ScriptValueType::Entity }),
        Arg("velocityX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
        Arg("velocityY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
        Arg("velocityZ", ScriptValue{ static_cast<float>(luaL_checknumber(state, 4)) }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("Physics.SetVelocity", arguments);
    lua_pushboolean(state, result.Output("applied").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaPhysicsGetVelocity(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    const std::vector<ScriptFunctionArgument> arguments{ Arg("entity", ScriptValue{ CheckEntityArg(state, 1), ScriptValueType::Entity }) };
    const ScriptFunctionCallResult result = context->CallFunction("Physics.GetVelocity", arguments);
    lua_createtable(state, 0, 4);
    for (const ScriptFunctionArgument& output : result.outputs) {
        PucLuaValueBridge::Push(state, output.value);
        lua_setfield(state, -2, output.name.c_str());
    }
    return 1;
}

int LuaPhysicsSetAngularVelocity(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("entity", ScriptValue{ CheckEntityArg(state, 1), ScriptValueType::Entity }),
        Arg("angularVelocityX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
        Arg("angularVelocityY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
        Arg("angularVelocityZ", ScriptValue{ static_cast<float>(luaL_checknumber(state, 4)) }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("Physics.SetAngularVelocity", arguments);
    lua_pushboolean(state, result.Output("applied").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaPhysicsGetAngularVelocity(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    const std::vector<ScriptFunctionArgument> arguments{ Arg("entity", ScriptValue{ CheckEntityArg(state, 1), ScriptValueType::Entity }) };
    const ScriptFunctionCallResult result = context->CallFunction("Physics.GetAngularVelocity", arguments);
    lua_createtable(state, 0, 4);
    for (const ScriptFunctionArgument& output : result.outputs) {
        PucLuaValueBridge::Push(state, output.value);
        lua_setfield(state, -2, output.name.c_str());
    }
    return 1;
}

int LuaPhysicsMoveKinematic(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("entity", ScriptValue{ CheckEntityArg(state, 1), ScriptValueType::Entity }),
        Arg("targetX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
        Arg("targetY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
        Arg("targetZ", ScriptValue{ static_cast<float>(luaL_checknumber(state, 4)) }),
        Arg("rotationX", ScriptValue{ static_cast<float>(luaL_optnumber(state, 5, 0.0)) }),
        Arg("rotationY", ScriptValue{ static_cast<float>(luaL_optnumber(state, 6, 0.0)) }),
        Arg("rotationZ", ScriptValue{ static_cast<float>(luaL_optnumber(state, 7, 0.0)) }),
        Arg("rotationW", ScriptValue{ static_cast<float>(luaL_optnumber(state, 8, 1.0)) }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("Physics.MoveKinematic", arguments);
    lua_pushboolean(state, result.Output("applied").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaPhysicsSleep(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const std::vector<ScriptFunctionArgument> arguments{ Arg("entity", ScriptValue{ CheckEntityArg(state, 1), ScriptValueType::Entity }) };
    const ScriptFunctionCallResult result = context->CallFunction("Physics.Sleep", arguments);
    lua_pushboolean(state, result.Output("applied").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaPhysicsWake(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const std::vector<ScriptFunctionArgument> arguments{ Arg("entity", ScriptValue{ CheckEntityArg(state, 1), ScriptValueType::Entity }) };
    const ScriptFunctionCallResult result = context->CallFunction("Physics.Wake", arguments);
    lua_pushboolean(state, result.Output("applied").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaPhysicsIsSleeping(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const std::vector<ScriptFunctionArgument> arguments{ Arg("entity", ScriptValue{ CheckEntityArg(state, 1), ScriptValueType::Entity }) };
    const ScriptFunctionCallResult result = context->CallFunction("Physics.IsSleeping", arguments);
    lua_pushboolean(state, result.Output("sleeping").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

// LIB-125: SphereCast/BoxCast/CapsuleCast/OverlapSphere/OverlapBox/
// OverlapCapsule take no entity argument, so (unlike the CheckEntityArg
// functions above) the generic ArgumentsFromTable path is safe here - there
// is no entity id for it to silently mis-marshal. Table-or-positional
// mirrors Raycast above; positional fallbacks match the native functions'
// own optional-pin defaults (Physics.SphereCast etc. in ScriptPhysicsApi.cpp).
[[nodiscard]] int PushShapeQueryLayerMaskDefault() noexcept {
    return static_cast<int>(kb::scene::kPhysicsAllLayers);
}

int LuaPhysicsSphereCast(lua_State* state) {
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
        arguments = {
            Arg("originX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 1)) }),
            Arg("originY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
            Arg("originZ", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
            Arg("directionX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 4)) }),
            Arg("directionY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 5)) }),
            Arg("directionZ", ScriptValue{ static_cast<float>(luaL_checknumber(state, 6)) }),
            Arg("distance", ScriptValue{ static_cast<float>(luaL_optnumber(state, 7, 1000.0)) }),
            Arg("radius", ScriptValue{ static_cast<float>(luaL_optnumber(state, 8, 0.5)) }),
            Arg("layerMask", ScriptValue{ static_cast<int>(luaL_optinteger(state, 9, PushShapeQueryLayerMaskDefault())) }),
        };
    }
    const ScriptFunctionCallResult result = context->CallFunction("Physics.SphereCast", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "physics sphere cast failed");
    }
    lua_createtable(state, 0, 9);
    for (const ScriptFunctionArgument& output : result.outputs) {
        PucLuaValueBridge::Push(state, output.value);
        lua_setfield(state, -2, output.name.c_str());
    }
    return 1;
}

int LuaPhysicsBoxCast(lua_State* state) {
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
        arguments = {
            Arg("originX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 1)) }),
            Arg("originY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
            Arg("originZ", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
            Arg("directionX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 4)) }),
            Arg("directionY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 5)) }),
            Arg("directionZ", ScriptValue{ static_cast<float>(luaL_checknumber(state, 6)) }),
            Arg("distance", ScriptValue{ static_cast<float>(luaL_optnumber(state, 7, 1000.0)) }),
            Arg("halfExtentsX", ScriptValue{ static_cast<float>(luaL_optnumber(state, 8, 0.5)) }),
            Arg("halfExtentsY", ScriptValue{ static_cast<float>(luaL_optnumber(state, 9, 0.5)) }),
            Arg("halfExtentsZ", ScriptValue{ static_cast<float>(luaL_optnumber(state, 10, 0.5)) }),
            Arg("layerMask", ScriptValue{ static_cast<int>(luaL_optinteger(state, 11, PushShapeQueryLayerMaskDefault())) }),
        };
    }
    const ScriptFunctionCallResult result = context->CallFunction("Physics.BoxCast", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "physics box cast failed");
    }
    lua_createtable(state, 0, 9);
    for (const ScriptFunctionArgument& output : result.outputs) {
        PucLuaValueBridge::Push(state, output.value);
        lua_setfield(state, -2, output.name.c_str());
    }
    return 1;
}

int LuaPhysicsCapsuleCast(lua_State* state) {
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
        arguments = {
            Arg("originX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 1)) }),
            Arg("originY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
            Arg("originZ", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
            Arg("directionX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 4)) }),
            Arg("directionY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 5)) }),
            Arg("directionZ", ScriptValue{ static_cast<float>(luaL_checknumber(state, 6)) }),
            Arg("distance", ScriptValue{ static_cast<float>(luaL_optnumber(state, 7, 1000.0)) }),
            Arg("radius", ScriptValue{ static_cast<float>(luaL_optnumber(state, 8, 0.5)) }),
            Arg("height", ScriptValue{ static_cast<float>(luaL_optnumber(state, 9, 2.0)) }),
            Arg("layerMask", ScriptValue{ static_cast<int>(luaL_optinteger(state, 10, PushShapeQueryLayerMaskDefault())) }),
        };
    }
    const ScriptFunctionCallResult result = context->CallFunction("Physics.CapsuleCast", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "physics capsule cast failed");
    }
    lua_createtable(state, 0, 9);
    for (const ScriptFunctionArgument& output : result.outputs) {
        PucLuaValueBridge::Push(state, output.value);
        lua_setfield(state, -2, output.name.c_str());
    }
    return 1;
}

int LuaPhysicsOverlapSphere(lua_State* state) {
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
        arguments = {
            Arg("centerX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 1)) }),
            Arg("centerY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
            Arg("centerZ", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
            Arg("radius", ScriptValue{ static_cast<float>(luaL_optnumber(state, 4, 0.5)) }),
            Arg("layerMask", ScriptValue{ static_cast<int>(luaL_optinteger(state, 5, PushShapeQueryLayerMaskDefault())) }),
        };
    }
    const ScriptFunctionCallResult result = context->CallFunction("Physics.OverlapSphere", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "physics overlap sphere failed");
    }
    lua_createtable(state, 0, 2);
    for (const ScriptFunctionArgument& output : result.outputs) {
        PucLuaValueBridge::Push(state, output.value);
        lua_setfield(state, -2, output.name.c_str());
    }
    return 1;
}

int LuaPhysicsOverlapBox(lua_State* state) {
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
        arguments = {
            Arg("centerX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 1)) }),
            Arg("centerY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
            Arg("centerZ", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
            Arg("halfExtentsX", ScriptValue{ static_cast<float>(luaL_optnumber(state, 4, 0.5)) }),
            Arg("halfExtentsY", ScriptValue{ static_cast<float>(luaL_optnumber(state, 5, 0.5)) }),
            Arg("halfExtentsZ", ScriptValue{ static_cast<float>(luaL_optnumber(state, 6, 0.5)) }),
            Arg("layerMask", ScriptValue{ static_cast<int>(luaL_optinteger(state, 7, PushShapeQueryLayerMaskDefault())) }),
        };
    }
    const ScriptFunctionCallResult result = context->CallFunction("Physics.OverlapBox", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "physics overlap box failed");
    }
    lua_createtable(state, 0, 2);
    for (const ScriptFunctionArgument& output : result.outputs) {
        PucLuaValueBridge::Push(state, output.value);
        lua_setfield(state, -2, output.name.c_str());
    }
    return 1;
}

int LuaPhysicsOverlapCapsule(lua_State* state) {
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
        arguments = {
            Arg("centerX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 1)) }),
            Arg("centerY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
            Arg("centerZ", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
            Arg("radius", ScriptValue{ static_cast<float>(luaL_optnumber(state, 4, 0.5)) }),
            Arg("height", ScriptValue{ static_cast<float>(luaL_optnumber(state, 5, 2.0)) }),
            Arg("layerMask", ScriptValue{ static_cast<int>(luaL_optinteger(state, 6, PushShapeQueryLayerMaskDefault())) }),
        };
    }
    const ScriptFunctionCallResult result = context->CallFunction("Physics.OverlapCapsule", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "physics overlap capsule failed");
    }
    lua_createtable(state, 0, 2);
    for (const ScriptFunctionArgument& output : result.outputs) {
        PucLuaValueBridge::Push(state, output.value);
        lua_setfield(state, -2, output.name.c_str());
    }
    return 1;
}

int LuaPhysicsClosestPoint(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("entity", ScriptValue{ CheckEntityArg(state, 1), ScriptValueType::Entity }),
        Arg("pointX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
        Arg("pointY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
        Arg("pointZ", ScriptValue{ static_cast<float>(luaL_checknumber(state, 4)) }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("Physics.ClosestPoint", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "physics closest point failed");
    }
    lua_createtable(state, 0, 5);
    for (const ScriptFunctionArgument& output : result.outputs) {
        PucLuaValueBridge::Push(state, output.value);
        lua_setfield(state, -2, output.name.c_str());
    }
    return 1;
}

[[nodiscard]] std::vector<ScriptFunctionArgument> InputActionArgs(lua_State* state) {
    const char* action = luaL_checkstring(state, 1);
    std::vector<ScriptFunctionArgument> arguments{ Arg("action", ScriptValue{ std::string{ action != nullptr ? action : "" } }) };
    const int player = static_cast<int>(luaL_optinteger(state, 2, 0));
    if (player > 0) {
        arguments.push_back(Arg("player", ScriptValue{ player }));
    }
    return arguments;
}

int LuaInputBool(lua_State* state, std::string_view functionName, std::string_view outputName) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const ScriptFunctionCallResult result = context->CallFunction(functionName, InputActionArgs(state));
    lua_pushboolean(state, result.Output(outputName).value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaInputIsPressed(lua_State* state) {
    return LuaInputBool(state, "Input.IsPressed", "pressed");
}

int LuaInputWasPressed(lua_State* state) {
    return LuaInputBool(state, "Input.WasPressed", "pressed");
}

int LuaInputWasReleased(lua_State* state) {
    return LuaInputBool(state, "Input.WasReleased", "released");
}

int LuaInputHeld(lua_State* state) {
    return LuaInputBool(state, "Input.Held", "held");
}

int LuaInputPressed(lua_State* state) {
    return LuaInputBool(state, "Input.Pressed", "pressed");
}

int LuaInputReleased(lua_State* state) {
    return LuaInputBool(state, "Input.Released", "released");
}

int LuaInputActionBool(lua_State* state) {
    return LuaInputBool(state, "Input.ActionBool", "value");
}

int LuaInputValue(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnumber(state, 0.0);
        return 1;
    }
    const ScriptFunctionCallResult result = context->CallFunction("Input.Value", InputActionArgs(state));
    lua_pushnumber(state, static_cast<lua_Number>(result.Output("value").value_or(ScriptValue{ 0.0F }).AsFloat()));
    return 1;
}

int LuaInputActionFloat(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnumber(state, 0.0);
        return 1;
    }
    const ScriptFunctionCallResult result = context->CallFunction("Input.ActionFloat", InputActionArgs(state));
    lua_pushnumber(state, static_cast<lua_Number>(result.Output("value").value_or(ScriptValue{ 0.0F }).AsFloat()));
    return 1;
}

int LuaInputVector2(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    const ScriptFunctionCallResult result = context->CallFunction("Input.Vector2", InputActionArgs(state));
    lua_createtable(state, 0, 2);
    lua_pushnumber(state, static_cast<lua_Number>(result.Output("x").value_or(ScriptValue{ 0.0F }).AsFloat()));
    lua_setfield(state, -2, "x");
    lua_pushnumber(state, static_cast<lua_Number>(result.Output("y").value_or(ScriptValue{ 0.0F }).AsFloat()));
    lua_setfield(state, -2, "y");
    return 1;
}

int LuaInputAction2D(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    const ScriptFunctionCallResult result = context->CallFunction("Input.Action2D", InputActionArgs(state));
    lua_createtable(state, 0, 2);
    lua_pushnumber(state, static_cast<lua_Number>(result.Output("x").value_or(ScriptValue{ 0.0F }).AsFloat()));
    lua_setfield(state, -2, "x");
    lua_pushnumber(state, static_cast<lua_Number>(result.Output("y").value_or(ScriptValue{ 0.0F }).AsFloat()));
    lua_setfield(state, -2, "y");
    return 1;
}

int LuaInputVector3(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    const ScriptFunctionCallResult result = context->CallFunction("Input.Vector3", InputActionArgs(state));
    lua_createtable(state, 0, 3);
    lua_pushnumber(state, static_cast<lua_Number>(result.Output("x").value_or(ScriptValue{ 0.0F }).AsFloat()));
    lua_setfield(state, -2, "x");
    lua_pushnumber(state, static_cast<lua_Number>(result.Output("y").value_or(ScriptValue{ 0.0F }).AsFloat()));
    lua_setfield(state, -2, "y");
    lua_pushnumber(state, static_cast<lua_Number>(result.Output("z").value_or(ScriptValue{ 0.0F }).AsFloat()));
    lua_setfield(state, -2, "z");
    return 1;
}

[[nodiscard]] std::string LuaContextId(lua_State* state, int index) {
    if (lua_isinteger(state, index) != 0) {
        return std::to_string(static_cast<std::uint64_t>(lua_tointeger(state, index)));
    }
    const char* context = luaL_checkstring(state, index);
    return std::string{ context != nullptr ? context : "" };
}

int LuaInputAddMappingContext(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const int priority = static_cast<int>(luaL_optinteger(state, 2, 0));
    const int player = static_cast<int>(luaL_optinteger(state, 3, 0));
    std::vector<ScriptFunctionArgument> arguments{
        Arg("context", ScriptValue{ LuaContextId(state, 1) }),
        Arg("priority", ScriptValue{ priority }),
    };
    if (player > 0) {
        arguments.push_back(Arg("player", ScriptValue{ player }));
    }
    const ScriptFunctionCallResult result = context->CallFunction("Input.AddMappingContext", arguments);
    lua_pushboolean(state, result.Output("added").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaInputRemoveMappingContext(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const int player = static_cast<int>(luaL_optinteger(state, 2, 0));
    std::vector<ScriptFunctionArgument> arguments{ Arg("context", ScriptValue{ LuaContextId(state, 1) }) };
    if (player > 0) {
        arguments.push_back(Arg("player", ScriptValue{ player }));
    }
    const ScriptFunctionCallResult result = context->CallFunction("Input.RemoveMappingContext", arguments);
    lua_pushboolean(state, result.Output("removed").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaPointerPosition(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    const ScriptFunctionCallResult result = context->CallFunction("Pointer.Position", {});
    lua_createtable(state, 0, 2);
    lua_pushnumber(state, static_cast<lua_Number>(result.Output("x").value_or(ScriptValue{ 0.0F }).AsFloat()));
    lua_setfield(state, -2, "x");
    lua_pushnumber(state, static_cast<lua_Number>(result.Output("y").value_or(ScriptValue{ 0.0F }).AsFloat()));
    lua_setfield(state, -2, "y");
    return 1;
}

int LuaPointerDelta(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    const ScriptFunctionCallResult result = context->CallFunction("Pointer.Delta", {});
    lua_createtable(state, 0, 2);
    lua_pushnumber(state, static_cast<lua_Number>(result.Output("x").value_or(ScriptValue{ 0.0F }).AsFloat()));
    lua_setfield(state, -2, "x");
    lua_pushnumber(state, static_cast<lua_Number>(result.Output("y").value_or(ScriptValue{ 0.0F }).AsFloat()));
    lua_setfield(state, -2, "y");
    return 1;
}

int LuaPointerButton(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const int button = static_cast<int>(luaL_optinteger(state, 1, 0));
    const std::vector<ScriptFunctionArgument> arguments{ Arg("button", ScriptValue{ button }) };
    const ScriptFunctionCallResult result = context->CallFunction("Pointer.Button", arguments);
    lua_pushboolean(state, result.Output("pressed").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaInputPriorityConstant(lua_State* state, std::string_view functionName) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushinteger(state, 0);
        return 1;
    }
    const ScriptFunctionCallResult result = context->CallFunction(functionName, {});
    lua_pushinteger(state, static_cast<lua_Integer>(result.Output("priority").value_or(ScriptValue{ 0 }).AsInt()));
    return 1;
}

int LuaInputPriorityGameplay(lua_State* state) {
    return LuaInputPriorityConstant(state, "Input.PriorityGameplay");
}

int LuaInputPriorityUI(lua_State* state) {
    return LuaInputPriorityConstant(state, "Input.PriorityUI");
}

int LuaInputPriorityConsole(lua_State* state) {
    return LuaInputPriorityConstant(state, "Input.PriorityConsole");
}

int LuaInputPriorityDebugOverlay(lua_State* state) {
    return LuaInputPriorityConstant(state, "Input.PriorityDebugOverlay");
}

int LuaInputHasFocus(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const ScriptFunctionCallResult result = context->CallFunction("Input.HasFocus", {});
    lua_pushboolean(state, result.Output("focus").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaInputIsGamepadConnected(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const int gamepadIndex = static_cast<int>(luaL_optinteger(state, 1, 0));
    const std::vector<ScriptFunctionArgument> arguments{ Arg("gamepadIndex", ScriptValue{ gamepadIndex }) };
    const ScriptFunctionCallResult result = context->CallFunction("Input.IsGamepadConnected", arguments);
    lua_pushboolean(state, result.Output("connected").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

void SetClosure(lua_State* state, const char* name, lua_CFunction function, ScriptExecutionContext& context) {
    lua_pushlightuserdata(state, &context);
    lua_pushcclosure(state, function, 1);
    lua_setfield(state, -2, name);
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

    lua_createtable(state, 0, 10);
    SetClosure(state, "FindByName", &LuaWorldFindByName, context);
    SetClosure(state, "FindByTag", &LuaWorldFindByTag, context);
    SetClosure(state, "Exists", &LuaWorldExists, context);
    SetClosure(state, "Name", &LuaWorldName, context);
    SetClosure(state, "Spawn", &LuaWorldSpawn, context);
    SetClosure(state, "Destroy", &LuaWorldDestroy, context);
    SetClosure(state, "SetTag", &LuaWorldSetTag, context);
    SetClosure(state, "HasTag", &LuaWorldHasTag, context);
    SetClosure(state, "SetParent", &LuaWorldSetParent, context);
    SetClosure(state, "InstantiatePrefab", &LuaWorldInstantiatePrefab, context);
    lua_setfield(state, environmentIndex, "World");

    lua_createtable(state, 0, 1);
    SetClosure(state, "delta", &LuaTimeDelta, context);
    lua_setfield(state, environmentIndex, "Time");

    lua_createtable(state, 0, 3);
    SetClosure(state, "GetPosition", &LuaTransformGetPosition, context);
    SetClosure(state, "SetPosition", &LuaTransformSetPosition, context);
    SetClosure(state, "Translate", &LuaTransformTranslate, context);
    lua_setfield(state, environmentIndex, "Transform");

    lua_createtable(state, 0, 18);
    SetClosure(state, "Raycast", &LuaPhysicsRaycast, context);
    SetClosure(state, "AddForce", &LuaPhysicsAddForce, context);
    SetClosure(state, "AddImpulse", &LuaPhysicsAddImpulse, context);
    SetClosure(state, "SetVelocity", &LuaPhysicsSetVelocity, context);
    SetClosure(state, "GetVelocity", &LuaPhysicsGetVelocity, context);
    SetClosure(state, "SetAngularVelocity", &LuaPhysicsSetAngularVelocity, context);
    SetClosure(state, "GetAngularVelocity", &LuaPhysicsGetAngularVelocity, context);
    SetClosure(state, "MoveKinematic", &LuaPhysicsMoveKinematic, context);
    SetClosure(state, "Sleep", &LuaPhysicsSleep, context);
    SetClosure(state, "Wake", &LuaPhysicsWake, context);
    SetClosure(state, "IsSleeping", &LuaPhysicsIsSleeping, context);
    SetClosure(state, "SphereCast", &LuaPhysicsSphereCast, context);
    SetClosure(state, "BoxCast", &LuaPhysicsBoxCast, context);
    SetClosure(state, "CapsuleCast", &LuaPhysicsCapsuleCast, context);
    SetClosure(state, "OverlapSphere", &LuaPhysicsOverlapSphere, context);
    SetClosure(state, "OverlapBox", &LuaPhysicsOverlapBox, context);
    SetClosure(state, "OverlapCapsule", &LuaPhysicsOverlapCapsule, context);
    SetClosure(state, "ClosestPoint", &LuaPhysicsClosestPoint, context);
    lua_setfield(state, environmentIndex, "Physics");

    lua_createtable(state, 0, 20);
    SetClosure(state, "IsPressed", &LuaInputIsPressed, context);
    SetClosure(state, "WasPressed", &LuaInputWasPressed, context);
    SetClosure(state, "WasReleased", &LuaInputWasReleased, context);
    SetClosure(state, "Value", &LuaInputValue, context);
    SetClosure(state, "Vector2", &LuaInputVector2, context);
    SetClosure(state, "Vector3", &LuaInputVector3, context);
    SetClosure(state, "AddMappingContext", &LuaInputAddMappingContext, context);
    SetClosure(state, "RemoveMappingContext", &LuaInputRemoveMappingContext, context);
    SetClosure(state, "ActionBool", &LuaInputActionBool, context);
    SetClosure(state, "ActionFloat", &LuaInputActionFloat, context);
    SetClosure(state, "Action2D", &LuaInputAction2D, context);
    SetClosure(state, "Pressed", &LuaInputPressed, context);
    SetClosure(state, "Released", &LuaInputReleased, context);
    SetClosure(state, "Held", &LuaInputHeld, context);
    SetClosure(state, "PriorityGameplay", &LuaInputPriorityGameplay, context);
    SetClosure(state, "PriorityUI", &LuaInputPriorityUI, context);
    SetClosure(state, "PriorityConsole", &LuaInputPriorityConsole, context);
    SetClosure(state, "PriorityDebugOverlay", &LuaInputPriorityDebugOverlay, context);
    SetClosure(state, "HasFocus", &LuaInputHasFocus, context);
    SetClosure(state, "IsGamepadConnected", &LuaInputIsGamepadConnected, context);
    lua_setfield(state, environmentIndex, "Input");

    lua_createtable(state, 0, 3);
    SetClosure(state, "Position", &LuaPointerPosition, context);
    SetClosure(state, "Delta", &LuaPointerDelta, context);
    SetClosure(state, "Button", &LuaPointerButton, context);
    lua_setfield(state, environmentIndex, "Pointer");
}

} // namespace kb::script
