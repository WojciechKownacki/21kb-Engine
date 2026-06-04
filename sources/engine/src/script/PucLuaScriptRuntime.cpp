#include "engine/script/PucLuaScriptRuntime.hpp"

#include "engine/script/ScriptSceneComponentApi.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <cstdint>
#include <algorithm>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kb::script {
namespace {

constexpr int kNoReference = LUA_NOREF;

class LuaStackScope final {
public:
    explicit LuaStackScope(lua_State* state) noexcept
        : state_(state)
        , top_(lua_gettop(state)) {}

    ~LuaStackScope() {
        lua_settop(state_, top_);
    }

    LuaStackScope(const LuaStackScope&) = delete;
    LuaStackScope& operator=(const LuaStackScope&) = delete;

private:
    lua_State* state_ = nullptr;
    int top_ = 0;
};

[[nodiscard]] std::string ErrorFromTop(lua_State* state) {
    const char* error = lua_tostring(state, -1);
    return error == nullptr ? std::string{"lua error"} : std::string{error};
}

int LuaTraceback(lua_State* state) {
    const char* message = lua_tostring(state, 1);
    if (message == nullptr) {
        message = "lua error";
    }
    luaL_traceback(state, state, message, 1);
    return 1;
}

[[nodiscard]] std::string ChunkFromDebug(lua_Debug& debug) {
    if (debug.source == nullptr) {
        return {};
    }
    std::string source{ debug.source };
    if (!source.empty() && source.front() == '@') {
        source.erase(source.begin());
    }
    return source;
}

void OpenSafeLibraries(lua_State* state) {
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

void PopulateSandboxEnvironment(lua_State* state, int environmentIndex) {
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

[[nodiscard]] ScriptExecutionContext* ContextFromUpvalue(lua_State* state) noexcept {
    return static_cast<ScriptExecutionContext*>(lua_touserdata(state, lua_upvalueindex(1)));
}

[[nodiscard]] PucLuaScriptRuntime* RuntimeFromUpvalue(lua_State* state) noexcept {
    return static_cast<PucLuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
}

[[nodiscard]] ScriptValue ValueFromLua(lua_State* state, int index) {
    switch (lua_type(state, index)) {
    case LUA_TBOOLEAN:
        return ScriptValue{lua_toboolean(state, index) != 0};
    case LUA_TNUMBER:
        if (lua_isinteger(state, index) != 0) {
            const lua_Integer value = lua_tointeger(state, index);
            if (value >= std::numeric_limits<int>::min() && value <= std::numeric_limits<int>::max()) {
                return ScriptValue{static_cast<int>(value)};
            }
            return ScriptValue{static_cast<float>(value)};
        }
        return ScriptValue{static_cast<float>(lua_tonumber(state, index))};
    case LUA_TSTRING:
        return ScriptValue{std::string{lua_tostring(state, index)}};
    default:
        break;
    }
    return ScriptValue{};
}

[[nodiscard]] ScriptValue DefaultScriptValue(ScriptValueType type) {
    switch (type) {
    case ScriptValueType::Bool:
        return ScriptValue{ false };
    case ScriptValueType::Int:
        return ScriptValue{ 0 };
    case ScriptValueType::Float:
        return ScriptValue{ 0.0F };
    case ScriptValueType::String:
        return ScriptValue{ std::string{} };
    case ScriptValueType::Entity:
        return ScriptValue{ 0U, ScriptValueType::Entity };
    case ScriptValueType::Component:
        return ScriptValue{ 0U, ScriptValueType::Component };
    case ScriptValueType::Void:
        break;
    }
    return ScriptValue{};
}

[[nodiscard]] PucLuaExposedVariableInstance* FindVariable(std::vector<PucLuaExposedVariableInstance>& variables, std::string_view name) noexcept {
    const auto iter = std::ranges::find_if(variables, [name](const PucLuaExposedVariableInstance& variable) {
        return variable.name == name;
    });
    return iter == variables.end() ? nullptr : &*iter;
}

[[nodiscard]] const PucLuaExposedVariableInstance* FindVariable(const std::vector<PucLuaExposedVariableInstance>& variables, std::string_view name) noexcept {
    const auto iter = std::ranges::find_if(variables, [name](const PucLuaExposedVariableInstance& variable) {
        return variable.name == name;
    });
    return iter == variables.end() ? nullptr : &*iter;
}

[[nodiscard]] const PucLuaExposedVariableInstance* FindVariable(std::span<const PucLuaExposedVariableInstance> variables, std::string_view name) noexcept {
    const auto iter = std::ranges::find_if(variables, [name](const PucLuaExposedVariableInstance& variable) {
        return variable.name == name;
    });
    return iter == variables.end() ? nullptr : &*iter;
}

[[nodiscard]] bool IsCompatible(ScriptValue value, ScriptValueType expected) noexcept {
    return value.Type() == expected || (expected == ScriptValueType::Float && value.Type() == ScriptValueType::Int) ||
           ((expected == ScriptValueType::Entity || expected == ScriptValueType::Component) && value.Type() == ScriptValueType::Int && value.AsInt() >= 0);
}

[[nodiscard]] ScriptValue CoerceScriptValue(ScriptValue value, ScriptValueType expected) {
    if (expected == ScriptValueType::Float && value.Type() == ScriptValueType::Int) {
        return ScriptValue{ static_cast<float>(value.AsInt()) };
    }
    if ((expected == ScriptValueType::Entity || expected == ScriptValueType::Component) && value.Type() == ScriptValueType::Int) {
        return ScriptValue{ static_cast<std::uint64_t>(value.AsInt()), expected };
    }
    return value;
}

[[nodiscard]] std::vector<ScriptEventArgument> ArgumentsFromLuaTable(lua_State* state, int index) {
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
                .value = ValueFromLua(state, -1),
            });
        }
        lua_pop(state, 1);
    }
    return arguments;
}

[[nodiscard]] std::vector<ScriptFunctionArgument> FunctionArgumentsFromLuaTable(lua_State* state, int index) {
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
                .value = ValueFromLua(state, -1),
            });
        }
        lua_pop(state, 1);
    }
    return arguments;
}

void PushScriptValue(lua_State* state, const ScriptValue& value);

int LuaEmit(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        return 0;
    }
    const char* eventName = luaL_checkstring(state, 1);
    std::vector<ScriptEventArgument> arguments;
    if (lua_gettop(state) >= 2 && lua_istable(state, 2) != 0) {
        arguments = ArgumentsFromLuaTable(state, 2);
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
        arguments = ArgumentsFromLuaTable(state, 3);
    }
    context->EmitTo(target, eventName, std::move(arguments));
    return 0;
}

int LuaSetShared(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const char* key = luaL_checkstring(state, 1);
    lua_pushboolean(state, context->SetSharedValue(key, ValueFromLua(state, 2)) ? 1 : 0);
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
    PushScriptValue(state, *value);
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
        arguments = FunctionArgumentsFromLuaTable(state, 2);
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
        PushScriptValue(state, result.outputs.front().value);
        return 1;
    }
    lua_createtable(state, 0, static_cast<int>(result.outputs.size()));
    for (const ScriptFunctionArgument& output : result.outputs) {
        PushScriptValue(state, output.value);
        lua_setfield(state, -2, output.name.c_str());
    }
    return 1;
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

int LuaSelfHasComponent(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const char* componentName = luaL_checkstring(state, 2);
    lua_pushboolean(state, ScriptSceneComponentApi::HasComponent(context->GetScene(), context->Self(), componentName) ? 1 : 0);
    return 1;
}

int LuaSelfGetProperty(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    const char* componentName = luaL_checkstring(state, 2);
    const char* propertyName = luaL_checkstring(state, 3);
    const ScriptSceneComponentPropertyResult result = ScriptSceneComponentApi::GetProperty(context->GetScene(), context->Self(), componentName, propertyName);
    if (!result.succeeded) {
        lua_pushnil(state);
        lua_pushlstring(state, result.error.data(), result.error.size());
        return 2;
    }
    PushScriptValue(state, result.value);
    return 1;
}

int LuaSelfSetProperty(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    const char* componentName = luaL_checkstring(state, 2);
    const char* propertyName = luaL_checkstring(state, 3);
    const ScriptValue value = ValueFromLua(state, 4);
    const ScriptSceneComponentMutationResult result = ScriptSceneComponentApi::SetProperty(context->GetScene(), context->Self(), componentName, propertyName, value);
    lua_pushboolean(state, result.succeeded ? 1 : 0);
    if (!result.succeeded) {
        lua_pushlstring(state, result.error.data(), result.error.size());
        return 2;
    }
    return 1;
}

int LuaSelfGetVariable(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    const char* variableName = luaL_checkstring(state, 2);
    PucLuaScriptRuntime* runtime = *static_cast<PucLuaScriptRuntime**>(lua_getextraspace(state));
    const std::span<const PucLuaExposedVariableInstance> variables = runtime == nullptr ? std::span<const PucLuaExposedVariableInstance>{} : runtime->InstanceVariables(context->Self(), context->Asset());
    const PucLuaExposedVariableInstance* variable = FindVariable(variables, variableName);
    if (variable == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "behaviour variable is not exposed");
        return 2;
    }
    PushScriptValue(state, variable->value);
    return 1;
}

int LuaSelfSetVariable(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    const char* variableName = luaL_checkstring(state, 2);
    PucLuaScriptRuntime* runtime = *static_cast<PucLuaScriptRuntime**>(lua_getextraspace(state));
    if (runtime == nullptr) {
        lua_pushboolean(state, 0);
        lua_pushliteral(state, "lua runtime is not available");
        return 2;
    }
    if (!runtime->SetInstanceVariable(context->Self(), context->Asset(), variableName, ValueFromLua(state, 3))) {
        lua_pushboolean(state, 0);
        lua_pushliteral(state, "behaviour variable is not exposed or has a type mismatch");
        return 2;
    }
    lua_pushboolean(state, 1);
    return 1;
}

void PushScriptValue(lua_State* state, const ScriptValue& value) {
    switch (value.Type()) {
    case ScriptValueType::Bool:
        lua_pushboolean(state, value.AsBool() ? 1 : 0);
        break;
    case ScriptValueType::Int:
        lua_pushinteger(state, static_cast<lua_Integer>(value.AsInt()));
        break;
    case ScriptValueType::Float:
        lua_pushnumber(state, static_cast<lua_Number>(value.AsFloat()));
        break;
    case ScriptValueType::String:
        lua_pushlstring(state, value.AsString().data(), value.AsString().size());
        break;
    case ScriptValueType::Entity:
    case ScriptValueType::Component:
        lua_pushinteger(state, static_cast<lua_Integer>(value.AsUInt64()));
        break;
    case ScriptValueType::Void:
        lua_pushnil(state);
        break;
    }
}

void PushVariables(lua_State* state, std::span<const PucLuaExposedVariableInstance> variables) {
    lua_createtable(state, 0, static_cast<int>(variables.size()));
    for (const PucLuaExposedVariableInstance& variable : variables) {
        if (variable.name.empty()) {
            continue;
        }
        PushScriptValue(state, variable.value);
        lua_setfield(state, -2, variable.name.c_str());
    }
}

void PushSelf(lua_State* state, ScriptExecutionContext& context) {
    lua_createtable(state, 0, 9);
    lua_pushinteger(state, static_cast<lua_Integer>(context.Self().Id()));
    lua_setfield(state, -2, "entity");
    lua_pushinteger(state, static_cast<lua_Integer>(context.Asset().value));
    lua_setfield(state, -2, "asset");
    lua_pushstring(state, "Lua");
    lua_setfield(state, -2, "backend");
    PucLuaScriptRuntime* runtime = *static_cast<PucLuaScriptRuntime**>(lua_getextraspace(state));
    if (runtime != nullptr) {
        PushVariables(state, runtime->InstanceVariables(context.Self(), context.Asset()));
    } else {
        lua_newtable(state);
    }
    lua_setfield(state, -2, "variables");
    lua_pushlightuserdata(state, &context);
    lua_pushcclosure(state, &LuaSelfHasComponent, 1);
    lua_setfield(state, -2, "HasComponent");
    lua_pushlightuserdata(state, &context);
    lua_pushcclosure(state, &LuaSelfGetProperty, 1);
    lua_setfield(state, -2, "GetProperty");
    lua_pushlightuserdata(state, &context);
    lua_pushcclosure(state, &LuaSelfSetProperty, 1);
    lua_setfield(state, -2, "SetProperty");
    lua_pushlightuserdata(state, &context);
    lua_pushcclosure(state, &LuaSelfGetVariable, 1);
    lua_setfield(state, -2, "GetVariable");
    lua_pushlightuserdata(state, &context);
    lua_pushcclosure(state, &LuaSelfSetVariable, 1);
    lua_setfield(state, -2, "SetVariable");
}

void PushEvent(lua_State* state, const ScriptEvent& event) {
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
        PushScriptValue(state, argument.value);
        lua_setfield(state, -2, argument.name.c_str());
    }
    lua_setfield(state, -2, "args");
}

void AttachRuntimeFunctions(lua_State* state, int environmentIndex, PucLuaScriptRuntime& runtime) {
    lua_pushlightuserdata(state, &runtime);
    lua_pushcclosure(state, &LuaImport, 1);
    lua_setfield(state, environmentIndex, "Import");
}

void AttachEmit(lua_State* state, int environmentIndex, ScriptExecutionContext& context, PucLuaScriptRuntime& runtime) {
    AttachRuntimeFunctions(state, environmentIndex, runtime);
    lua_pushlightuserdata(state, &context);
    lua_pushcclosure(state, &LuaEmit, 1);
    lua_setfield(state, environmentIndex, "Emit");
    lua_pushlightuserdata(state, &context);
    lua_pushcclosure(state, &LuaEmitTo, 1);
    lua_setfield(state, environmentIndex, "EmitTo");
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
    lua_pushlightuserdata(state, &context);
    lua_pushcclosure(state, &LuaCallFunction, 1);
    lua_setfield(state, environmentIndex, "CallFunction");
}

void CreateEnvironment(lua_State* state) {
    lua_newtable(state);
    const int environmentIndex = lua_gettop(state);
    PopulateSandboxEnvironment(state, environmentIndex);
}

[[nodiscard]] bool IsBreakpointMatch(std::string_view configuredChunk, std::string_view actualChunk) noexcept {
    if (configuredChunk.empty()) {
        return true;
    }
    return actualChunk == configuredChunk || actualChunk.ends_with(configuredChunk);
}

[[nodiscard]] PucLuaDebugVariableSnapshot LuaDebugVariable(lua_State* state, const char* name, int valueIndex) {
    PucLuaDebugVariableSnapshot variable{
        .name = name == nullptr ? std::string{} : std::string{ name },
        .value = "<unsupported>",
        .type = ScriptValueType::Void,
    };
    switch (lua_type(state, valueIndex)) {
    case LUA_TBOOLEAN:
        variable.value = lua_toboolean(state, valueIndex) != 0 ? "true" : "false";
        variable.type = ScriptValueType::Bool;
        break;
    case LUA_TNUMBER:
        if (lua_isinteger(state, valueIndex) != 0) {
            variable.value = std::to_string(static_cast<int>(lua_tointeger(state, valueIndex)));
            variable.type = ScriptValueType::Int;
        } else {
            variable.value = std::to_string(static_cast<float>(lua_tonumber(state, valueIndex)));
            variable.type = ScriptValueType::Float;
        }
        break;
    case LUA_TSTRING:
        variable.value = lua_tostring(state, valueIndex);
        variable.type = ScriptValueType::String;
        break;
    case LUA_TNIL:
        variable.value = "nil";
        break;
    default:
        variable.value = luaL_typename(state, valueIndex);
        break;
    }
    return variable;
}

[[nodiscard]] PucLuaDebugPauseSnapshot CaptureDebugPause(
    lua_State* state,
    PucLuaDebugPauseReason reason,
    const PucLuaDebugSettings& settings,
    const lua_Debug& currentDebug) {
    PucLuaDebugPauseSnapshot snapshot{
        .valid = true,
        .reason = reason,
        .chunkName = ChunkFromDebug(const_cast<lua_Debug&>(currentDebug)),
        .line = currentDebug.currentline,
    };
    if (!settings.collectCallStack) {
        return snapshot;
    }
    lua_Debug frame{};
    for (int level = 0; lua_getstack(state, level, &frame) != 0; ++level) {
        lua_getinfo(state, "nSl", &frame);
        PucLuaDebugFrameSnapshot frameSnapshot{
            .name = frame.name == nullptr ? std::string{} : std::string{ frame.name },
            .chunkName = ChunkFromDebug(frame),
            .line = frame.currentline,
        };
        if (settings.collectLocals) {
            for (int localIndex = 1;; ++localIndex) {
                const char* localName = lua_getlocal(state, &frame, localIndex);
                if (localName == nullptr) {
                    break;
                }
                if (localName[0] != '(') {
                    frameSnapshot.locals.push_back(LuaDebugVariable(state, localName, -1));
                }
                lua_pop(state, 1);
            }
        }
        snapshot.callStack.push_back(std::move(frameSnapshot));
    }
    return snapshot;
}

void LuaDebugHook(lua_State* state, lua_Debug* debug) {
    auto* runtime = *static_cast<PucLuaScriptRuntime**>(lua_getextraspace(state));
    if (runtime == nullptr || debug == nullptr) {
        return;
    }
    lua_getinfo(state, "Sl", debug);
    const std::string chunk = ChunkFromDebug(*debug);
    const PucLuaDebugSettings& settings = runtime->DebugSettings();
    bool shouldPause = false;
    PucLuaDebugPauseReason reason = PucLuaDebugPauseReason::Breakpoint;
    if (const std::optional<PucLuaDebugPauseReason> requested = runtime->ConsumeRequestedDebugPause(); requested.has_value()) {
        shouldPause = true;
        reason = *requested;
    }
    if (settings.enableBreakpoints) {
        for (const PucLuaDebugBreakpoint& breakpoint : settings.breakpoints) {
            if (!breakpoint.enabled || breakpoint.line != debug->currentline || !IsBreakpointMatch(breakpoint.chunkName, chunk)) {
                continue;
            }
            shouldPause = true;
            reason = PucLuaDebugPauseReason::Breakpoint;
            break;
        }
    }
    if (!shouldPause) {
        return;
    }
    runtime->RecordDebugPause(CaptureDebugPause(state, reason, settings, *debug));
    if (settings.stopOnBreakpoint) {
        const char* label = "lua breakpoint hit";
        if (reason == PucLuaDebugPauseReason::ManualBreak) {
            label = "lua manual break";
        } else if (reason == PucLuaDebugPauseReason::Step) {
            label = "lua step break";
        }
        luaL_error(state, "%s at %s:%d", label, chunk.c_str(), debug->currentline);
    }
}

void InstallDebugHook(lua_State* state, const PucLuaScriptRuntime& runtime) {
    if (runtime.NeedsDebugHook()) {
        lua_sethook(state, &LuaDebugHook, LUA_MASKLINE, 0);
    }
}

void ClearDebugHook(lua_State* state) {
    lua_sethook(state, nullptr, 0, 0);
}

} // namespace

PucLuaScriptRuntime::PucLuaScriptRuntime()
    : state_(luaL_newstate()) {
    if (state_ != nullptr) {
        *static_cast<PucLuaScriptRuntime**>(lua_getextraspace(state_)) = this;
        OpenSafeLibraries(state_);
    }
}

PucLuaScriptRuntime::~PucLuaScriptRuntime() {
    if (state_ != nullptr) {
        lua_close(state_);
        state_ = nullptr;
    }
}

PucLuaLoadResult PucLuaScriptRuntime::LoadScript(kb::assets::AssetId assetId, std::string_view source, std::string_view chunkName) {
    return LoadScript(assetId, source, chunkName, 0U);
}

PucLuaLoadResult PucLuaScriptRuntime::LoadScript(kb::assets::AssetId assetId, std::string_view source, std::string_view chunkName, std::uint64_t contentHash) {
    return ReloadScript(assetId, source, chunkName, contentHash);
}

PucLuaLoadResult PucLuaScriptRuntime::ReloadScript(kb::assets::AssetId assetId, std::string_view source, std::string_view chunkName, std::uint64_t contentHash) {
    if (state_ == nullptr) {
        return PucLuaLoadResult{.error = "lua state could not be created"};
    }
    if (!assetId.IsValid()) {
        return PucLuaLoadResult{.error = "lua script asset id is invalid"};
    }

    LuaStackScope stack{state_};
    CreateEnvironment(state_);
    const int environmentIndex = lua_gettop(state_);
    AttachRuntimeFunctions(state_, environmentIndex, *this);

    const std::string chunk = chunkName.empty() ? std::string{"lua-script-"} + std::to_string(assetId.value) : std::string{chunkName};
    if (luaL_loadbufferx(state_, source.data(), source.size(), chunk.c_str(), "t") != LUA_OK) {
        return PucLuaLoadResult{.error = ErrorFromTop(state_)};
    }

    lua_pushvalue(state_, environmentIndex);
    static_cast<void>(lua_setupvalue(state_, -2, 1));
    lua_pushcfunction(state_, &LuaTraceback);
    const int errorHandlerIndex = lua_gettop(state_) - 1;
    lua_insert(state_, errorHandlerIndex);
    InstallDebugHook(state_, *this);
    const int status = lua_pcall(state_, 0, 0, errorHandlerIndex);
    ClearDebugHook(state_);
    if (status != LUA_OK) {
        return PucLuaLoadResult{.error = ErrorFromTop(state_)};
    }
    lua_remove(state_, errorHandlerIndex);

    lua_pushvalue(state_, environmentIndex);
    const int environmentRef = luaL_ref(state_, LUA_REGISTRYINDEX);
    const auto old = scripts_.find(assetId.value);
    if (old != scripts_.end()) {
        luaL_unref(state_, LUA_REGISTRYINDEX, old->second.environmentRef);
    }
    scripts_[assetId.value] = ScriptRecord{
        .environmentRef = environmentRef,
        .chunkName = chunk,
        .contentHash = contentHash,
        .generation = generation_++,
    };
    return PucLuaLoadResult{.succeeded = true};
}

void PucLuaScriptRuntime::UnloadScript(kb::assets::AssetId assetId) noexcept {
    if (state_ == nullptr) {
        return;
    }
    const auto iter = scripts_.find(assetId.value);
    if (iter == scripts_.end()) {
        return;
    }
    luaL_unref(state_, LUA_REGISTRYINDEX, iter->second.environmentRef);
    scripts_.erase(iter);
    exposedVariables_.erase(assetId.value);
    std::erase_if(instanceVariables_, [assetId](const auto& entry) {
        return entry.first.assetId == assetId.value;
    });
}

void PucLuaScriptRuntime::Clear() noexcept {
    if (state_ != nullptr) {
        for (const auto& [assetId, record] : scripts_) {
            static_cast<void>(assetId);
            luaL_unref(state_, LUA_REGISTRYINDEX, record.environmentRef);
        }
        for (const auto& [name, record] : modules_) {
            static_cast<void>(name);
            if (record.valueRef != kNoReference) {
                luaL_unref(state_, LUA_REGISTRYINDEX, record.valueRef);
            }
        }
    }
    scripts_.clear();
    modules_.clear();
    exposedVariables_.clear();
    instanceVariables_.clear();
    lastDebugPause_ = {};
    debugStepMode_ = DebugStepMode::Run;
}

bool PucLuaScriptRuntime::HasScript(kb::assets::AssetId assetId) const noexcept {
    return scripts_.contains(assetId.value);
}

bool PucLuaScriptRuntime::IsScriptCurrent(kb::assets::AssetId assetId, std::uint64_t contentHash) const noexcept {
    const auto iter = scripts_.find(assetId.value);
    return iter != scripts_.end() && (contentHash == 0U || iter->second.contentHash == contentHash);
}

PucLuaLoadResult PucLuaScriptRuntime::RegisterModule(std::string name, std::string source, std::string chunkName) {
    return RegisterModule(std::move(name), std::move(source), std::move(chunkName), 0U);
}

PucLuaLoadResult PucLuaScriptRuntime::RegisterModule(std::string name, std::string source, std::string chunkName, std::uint64_t contentHash) {
    if (state_ == nullptr) {
        return PucLuaLoadResult{ .error = "lua state could not be created" };
    }
    if (name.empty()) {
        return PucLuaLoadResult{ .error = "lua module name is empty" };
    }
    UnloadModule(name);
    modules_[name] = ModuleRecord{
        .source = std::move(source),
        .chunkName = chunkName.empty() ? name : std::move(chunkName),
        .valueRef = kNoReference,
        .contentHash = contentHash,
        .generation = generation_++,
    };
    return PucLuaLoadResult{ .succeeded = true };
}

void PucLuaScriptRuntime::UnloadModule(std::string_view name) noexcept {
    if (state_ == nullptr) {
        return;
    }
    const auto iter = modules_.find(std::string{ name });
    if (iter == modules_.end()) {
        return;
    }
    if (iter->second.valueRef != kNoReference) {
        luaL_unref(state_, LUA_REGISTRYINDEX, iter->second.valueRef);
    }
    modules_.erase(iter);
}

void PucLuaScriptRuntime::ClearModules() noexcept {
    if (state_ != nullptr) {
        for (const auto& [name, module] : modules_) {
            static_cast<void>(name);
            if (module.valueRef != kNoReference) {
                luaL_unref(state_, LUA_REGISTRYINDEX, module.valueRef);
            }
        }
    }
    modules_.clear();
}

bool PucLuaScriptRuntime::HasModule(std::string_view name) const noexcept {
    return modules_.contains(std::string{ name });
}

bool PucLuaScriptRuntime::IsModuleCurrent(std::string_view name, std::uint64_t contentHash) const noexcept {
    const auto iter = modules_.find(std::string{ name });
    return iter != modules_.end() && (contentHash == 0U || iter->second.contentHash == contentHash);
}

void PucLuaScriptRuntime::SetScriptExposedVariables(
    kb::assets::AssetId assetId,
    std::span<const ScriptApiPin> variables,
    std::span<const ScriptValue> defaults,
    std::span<const std::uint8_t> hasDefaults) {
    if (!assetId.IsValid()) {
        return;
    }
    std::vector<ExposedVariableRecord> records;
    records.reserve(variables.size());
    for (std::size_t index = 0U; index < variables.size(); ++index) {
        if (variables[index].name.empty() || variables[index].type == ScriptValueType::Void) {
            continue;
        }
        const ScriptValue defaultValue = index < defaults.size() ? defaults[index] : DefaultScriptValue(variables[index].type);
        records.push_back(ExposedVariableRecord{
            .pin = variables[index],
            .defaultValue = defaultValue.Type() == ScriptValueType::Void ? DefaultScriptValue(variables[index].type) : defaultValue,
            .hasDefault = index < hasDefaults.size() && hasDefaults[index] != 0U,
        });
    }
    exposedVariables_[assetId.value] = std::move(records);
}

std::span<const PucLuaExposedVariableInstance> PucLuaScriptRuntime::InstanceVariables(kb::scene::SceneEntity entity, kb::assets::AssetId assetId) const noexcept {
    const auto iter = instanceVariables_.find(InstanceKey{
        .entityId = entity.Id(),
        .assetId = assetId.value,
    });
    if (iter == instanceVariables_.end()) {
        return {};
    }
    return iter->second;
}

bool PucLuaScriptRuntime::SetInstanceVariable(kb::scene::SceneEntity entity, kb::assets::AssetId assetId, std::string_view name, ScriptValue value) {
    const auto iter = instanceVariables_.find(InstanceKey{
        .entityId = entity.Id(),
        .assetId = assetId.value,
    });
    if (iter == instanceVariables_.end()) {
        return false;
    }
    PucLuaExposedVariableInstance* variable = FindVariable(iter->second, name);
    if (variable == nullptr || !IsCompatible(value, variable->type)) {
        return false;
    }
    variable->value = CoerceScriptValue(std::move(value), variable->type);
    variable->overridden = true;
    return true;
}

void PucLuaScriptRuntime::SetDebugSettings(PucLuaDebugSettings settings) {
    debugSettings_ = std::move(settings);
}

const PucLuaDebugSettings& PucLuaScriptRuntime::DebugSettings() const noexcept {
    return debugSettings_;
}

void PucLuaScriptRuntime::RequestBreakOnNextLine() noexcept {
    debugStepMode_ = DebugStepMode::BreakOnNextLine;
}

void PucLuaScriptRuntime::RequestStepInto() noexcept {
    debugStepMode_ = DebugStepMode::StepInto;
}

void PucLuaScriptRuntime::ResumeDebugExecution() noexcept {
    debugStepMode_ = DebugStepMode::Run;
}

void PucLuaScriptRuntime::RecordDebugPause(PucLuaDebugPauseSnapshot snapshot) {
    lastDebugPause_ = std::move(snapshot);
}

std::optional<PucLuaDebugPauseReason> PucLuaScriptRuntime::ConsumeRequestedDebugPause() noexcept {
    switch (debugStepMode_) {
    case DebugStepMode::BreakOnNextLine:
        debugStepMode_ = DebugStepMode::Run;
        return PucLuaDebugPauseReason::ManualBreak;
    case DebugStepMode::StepInto:
        debugStepMode_ = DebugStepMode::Run;
        return PucLuaDebugPauseReason::Step;
    case DebugStepMode::Run:
        break;
    }
    return std::nullopt;
}

bool PucLuaScriptRuntime::NeedsDebugHook() const noexcept {
    return debugStepMode_ == DebugStepMode::BreakOnNextLine ||
           debugStepMode_ == DebugStepMode::StepInto ||
           (debugSettings_.enableBreakpoints && !debugSettings_.breakpoints.empty());
}

const PucLuaDebugPauseSnapshot& PucLuaScriptRuntime::LastDebugPause() const noexcept {
    return lastDebugPause_;
}

void PucLuaScriptRuntime::ClearDebugPause() noexcept {
    lastDebugPause_ = {};
}

bool PucLuaScriptRuntime::PushModuleForImport(std::string_view name, std::string& error) {
    const auto iter = modules_.find(std::string{ name });
    if (iter == modules_.end()) {
        error = "lua module is not registered: " + std::string{ name };
        return false;
    }
    ModuleRecord& module = iter->second;
    if (module.valueRef != kNoReference) {
        lua_rawgeti(state_, LUA_REGISTRYINDEX, module.valueRef);
        return true;
    }
    if (module.loading) {
        error = "lua circular module import detected: " + std::string{ name };
        return false;
    }

    const int originalTop = lua_gettop(state_);
    module.loading = true;
    CreateEnvironment(state_);
    const int environmentIndex = lua_gettop(state_);
    AttachRuntimeFunctions(state_, environmentIndex, *this);
    if (luaL_loadbufferx(state_, module.source.data(), module.source.size(), module.chunkName.c_str(), "t") != LUA_OK) {
        error = ErrorFromTop(state_);
        module.loading = false;
        lua_settop(state_, originalTop);
        return false;
    }
    lua_pushvalue(state_, environmentIndex);
    static_cast<void>(lua_setupvalue(state_, -2, 1));
    lua_pushcfunction(state_, &LuaTraceback);
    const int errorHandlerIndex = lua_gettop(state_) - 1;
    lua_insert(state_, errorHandlerIndex);
    InstallDebugHook(state_, *this);
    const int status = lua_pcall(state_, 0, 1, errorHandlerIndex);
    ClearDebugHook(state_);
    if (status != LUA_OK) {
        error = ErrorFromTop(state_);
        module.loading = false;
        lua_settop(state_, originalTop);
        return false;
    }
    if (lua_isnil(state_, -1) != 0) {
        lua_pop(state_, 1);
        lua_pushvalue(state_, environmentIndex);
    }
    module.valueRef = luaL_ref(state_, LUA_REGISTRYINDEX);
    module.loading = false;
    lua_settop(state_, originalTop);
    lua_rawgeti(state_, LUA_REGISTRYINDEX, module.valueRef);
    return true;
}

ScriptBackendExecutionResult PucLuaScriptRuntime::ExecuteLifecycle(const kb::scene::BehaviourComponent& behaviour, ScriptExecutionContext& context) {
    return ExecuteFunction(behaviour, ToString(context.Lifecycle()), context, nullptr);
}

ScriptBackendExecutionResult PucLuaScriptRuntime::ExecuteEvent(const kb::scene::BehaviourComponent& behaviour, const ScriptEvent& event, ScriptExecutionContext& context) {
    return ExecuteFunction(behaviour, event.name, context, &event);
}

ScriptBackendExecutionResult PucLuaScriptRuntime::ExecuteFunction(
    const kb::scene::BehaviourComponent& behaviour,
    std::string_view functionName,
    ScriptExecutionContext& context,
    const ScriptEvent* event) {
    ScriptBackendExecutionResult result{};
    const kb::assets::AssetId assetId{behaviour.behaviourAssetId};
    const InstanceKey instanceKey{
        .entityId = context.Self().Id(),
        .assetId = assetId.value,
    };
    const auto eraseDestroyedInstanceState = [&]() {
        if (context.Lifecycle() == ScriptLifecycleEvent::Destroyed) {
            instanceVariables_.erase(instanceKey);
        }
    };
    const int environmentRef = FindScriptEnvironment(assetId);
    if (environmentRef == kNoReference) {
        result.diagnostics.push_back(ScriptDiagnostic{
            .entity = context.Self(),
            .assetId = assetId,
            .backend = behaviour.backend,
            .message = "lua script is not loaded",
        });
        eraseDestroyedInstanceState();
        return result;
    }

    if (const auto definitions = exposedVariables_.find(assetId.value); definitions != exposedVariables_.end()) {
        std::vector<PucLuaExposedVariableInstance>& variables = instanceVariables_[instanceKey];
        std::vector<std::string_view> declaredNames;
        declaredNames.reserve(definitions->second.size());
        for (const ExposedVariableRecord& definition : definitions->second) {
            declaredNames.push_back(definition.pin.name);
            PucLuaExposedVariableInstance* variable = FindVariable(variables, definition.pin.name);
            if (variable == nullptr) {
                const ScriptValue value = definition.hasDefault ? definition.defaultValue : DefaultScriptValue(definition.pin.type);
                variables.push_back(PucLuaExposedVariableInstance{
                    .name = definition.pin.name,
                    .type = definition.pin.type,
                    .value = value,
                    .overridden = false,
                });
                continue;
            }
            if (variable->type != definition.pin.type) {
                if (variable->overridden) {
                    result.diagnostics.push_back(ScriptDiagnostic{
                        .entity = context.Self(),
                        .assetId = assetId,
                        .backend = behaviour.backend,
                        .message = "lua exposed variable type conflict for '" + variable->name + "'",
                    });
                    eraseDestroyedInstanceState();
                    return result;
                }
                const ScriptValue value = definition.hasDefault ? definition.defaultValue : DefaultScriptValue(definition.pin.type);
                variable->type = definition.pin.type;
                variable->value = value;
                variable->overridden = false;
                continue;
            }
            if (!variable->overridden) {
                variable->value = definition.hasDefault ? definition.defaultValue : DefaultScriptValue(definition.pin.type);
            }
        }
        variables.erase(
            std::remove_if(variables.begin(), variables.end(), [&declaredNames](const PucLuaExposedVariableInstance& variable) {
                return !variable.overridden && std::ranges::find(declaredNames, std::string_view{ variable.name }) == declaredNames.end();
            }),
            variables.end());
    }

    LuaStackScope stack{state_};
    lua_rawgeti(state_, LUA_REGISTRYINDEX, environmentRef);
    const int environmentIndex = lua_gettop(state_);
    AttachEmit(state_, environmentIndex, context, *this);
    lua_getfield(state_, environmentIndex, std::string{functionName}.c_str());
    if (lua_isnil(state_, -1) != 0) {
        eraseDestroyedInstanceState();
        return result;
    }
    if (lua_isfunction(state_, -1) == 0) {
        result.diagnostics.push_back(ScriptDiagnostic{
            .entity = context.Self(),
            .assetId = assetId,
            .backend = behaviour.backend,
            .message = "lua script entry is not a function",
        });
        eraseDestroyedInstanceState();
        return result;
    }

    lua_pushcfunction(state_, &LuaTraceback);
    const int errorHandlerIndex = lua_gettop(state_) - 1;
    lua_insert(state_, errorHandlerIndex);

    PushSelf(state_, context);
    int argCount = 1;
    if (event == nullptr) {
        lua_pushnumber(state_, static_cast<lua_Number>(context.DeltaSeconds()));
        ++argCount;
    } else {
        PushEvent(state_, *event);
        ++argCount;
    }

    InstallDebugHook(state_, *this);
    const int status = lua_pcall(state_, argCount, 0, errorHandlerIndex);
    ClearDebugHook(state_);
    if (status != LUA_OK) {
        result.diagnostics.push_back(ScriptDiagnostic{
            .entity = context.Self(),
            .assetId = assetId,
            .backend = behaviour.backend,
            .message = ErrorFromTop(state_),
        });
        eraseDestroyedInstanceState();
        return result;
    }

    result.executed = true;
    eraseDestroyedInstanceState();
    return result;
}

int PucLuaScriptRuntime::FindScriptEnvironment(kb::assets::AssetId assetId) const noexcept {
    const auto iter = scripts_.find(assetId.value);
    return iter == scripts_.end() ? kNoReference : iter->second.environmentRef;
}

} // namespace kb::script
