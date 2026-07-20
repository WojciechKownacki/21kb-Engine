#include "script/lua/api/PucLuaSelfApi.hpp"

#include "engine/script/PucLuaSafeCall.hpp"
#include "engine/script/PucLuaScriptRuntime.hpp"
#include "engine/script/ScriptExecutionContext.hpp"
#include "engine/script/ScriptSceneComponentApi.hpp"
#include "script/lua/PucLuaValueBridge.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <span>

namespace kb::script {
namespace {

[[nodiscard]] ScriptExecutionContext* ContextFromUpvalue(lua_State* state) noexcept {
    return static_cast<ScriptExecutionContext*>(lua_touserdata(state, lua_upvalueindex(1)));
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
    PucLuaValueBridge::Push(state, result.value);
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
    const ScriptValue value = PucLuaValueBridge::FromLua(state, 4);
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
    const std::span<const PucLuaExposedVariableInstance> variables =
        runtime == nullptr ? std::span<const PucLuaExposedVariableInstance>{} : runtime->InstanceVariables(context->Self(), context->Asset());
    const PucLuaExposedVariableInstance* variable = PucLuaValueBridge::FindVariable(variables, variableName);
    if (variable == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "behaviour variable is not exposed");
        return 2;
    }
    PucLuaValueBridge::Push(state, variable->value);
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
    if (!runtime->SetInstanceVariable(context->Self(), context->Asset(), variableName, PucLuaValueBridge::FromLua(state, 3))) {
        lua_pushboolean(state, 0);
        lua_pushliteral(state, "behaviour variable is not exposed or has a type mismatch");
        return 2;
    }
    lua_pushboolean(state, 1);
    return 1;
}

void PushVariables(lua_State* state, std::span<const PucLuaExposedVariableInstance> variables) {
    lua_createtable(state, 0, static_cast<int>(variables.size()));
    for (const PucLuaExposedVariableInstance& variable : variables) {
        if (variable.name.empty()) {
            continue;
        }
        PucLuaValueBridge::Push(state, variable.value);
        lua_setfield(state, -2, variable.name.c_str());
    }
}

// `Inspector.name` read. Upvalue 1 is the execution context (nullptr at chunk
// load). With a context it returns the CURRENT entity's exposed-variable value
// (override ?? default) — the per-entity semantics, identical to Self:GetVariable
// keyed by the field name. Without a context (declaration time) it is nil.
int LuaInspectorIndex(lua_State* state) {
    ScriptExecutionContext* context = static_cast<ScriptExecutionContext*>(lua_touserdata(state, lua_upvalueindex(1)));
    if (context == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    const char* variableName = luaL_checkstring(state, 2);
    PucLuaScriptRuntime* runtime = *static_cast<PucLuaScriptRuntime**>(lua_getextraspace(state));
    const std::span<const PucLuaExposedVariableInstance> variables =
        runtime == nullptr ? std::span<const PucLuaExposedVariableInstance>{} : runtime->InstanceVariables(context->Self(), context->Asset());
    const PucLuaExposedVariableInstance* variable = PucLuaValueBridge::FindVariable(variables, variableName);
    if (variable == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    PucLuaValueBridge::Push(state, variable->value);
    return 1;
}

// `Inspector.name = value` write. At chunk load (context == nullptr) it is a
// no-op: the declaration's name/type/default are parsed statically from the
// source, so the top-level assignment must not error. During execution it writes
// the CURRENT entity's exposed-variable instance (persisting like Self:SetVariable
// — the value survives the per-frame default re-sync).
int LuaInspectorNewIndex(lua_State* state) {
    ScriptExecutionContext* context = static_cast<ScriptExecutionContext*>(lua_touserdata(state, lua_upvalueindex(1)));
    if (context == nullptr) {
        return 0;
    }
    const char* variableName = luaL_checkstring(state, 2);
    PucLuaScriptRuntime* runtime = *static_cast<PucLuaScriptRuntime**>(lua_getextraspace(state));
    if (runtime != nullptr) {
        static_cast<void>(runtime->SetInstanceVariable(context->Self(), context->Asset(), variableName, PucLuaValueBridge::FromLua(state, 3)));
    }
    return 0;
}

} // namespace

void PucLuaSelfApi::PushSelf(lua_State* state, ScriptExecutionContext& context) {
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
    lua_pushcclosure(state, &PucLuaSafeCall<&LuaSelfHasComponent>, 1);
    lua_setfield(state, -2, "HasComponent");
    lua_pushlightuserdata(state, &context);
    lua_pushcclosure(state, &PucLuaSafeCall<&LuaSelfGetProperty>, 1);
    lua_setfield(state, -2, "GetProperty");
    lua_pushlightuserdata(state, &context);
    lua_pushcclosure(state, &PucLuaSafeCall<&LuaSelfSetProperty>, 1);
    lua_setfield(state, -2, "SetProperty");
    lua_pushlightuserdata(state, &context);
    lua_pushcclosure(state, &PucLuaSafeCall<&LuaSelfGetVariable>, 1);
    lua_setfield(state, -2, "GetVariable");
    lua_pushlightuserdata(state, &context);
    lua_pushcclosure(state, &PucLuaSafeCall<&LuaSelfSetVariable>, 1);
    lua_setfield(state, -2, "SetVariable");
}

void PucLuaSelfApi::AttachInspector(lua_State* state, int environmentIndex, ScriptExecutionContext* context) {
    lua_newtable(state); // the Inspector proxy table — kept empty so every field access hits the metatable
    lua_createtable(state, 0, 2);
    lua_pushlightuserdata(state, context);
    lua_pushcclosure(state, &PucLuaSafeCall<&LuaInspectorIndex>, 1);
    lua_setfield(state, -2, "__index");
    lua_pushlightuserdata(state, context);
    lua_pushcclosure(state, &PucLuaSafeCall<&LuaInspectorNewIndex>, 1);
    lua_setfield(state, -2, "__newindex");
    lua_setmetatable(state, -2);
    lua_setfield(state, environmentIndex, "Inspector");
}

} // namespace kb::script
