#include "script/lua/PucLuaValueBridge.hpp"

extern "C" {
#include <lua.h>
}

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>

namespace kb::script {

ScriptValue PucLuaValueBridge::FromLua(lua_State* state, int index) {
    switch (lua_type(state, index)) {
    case LUA_TBOOLEAN:
        return ScriptValue{ lua_toboolean(state, index) != 0 };
    case LUA_TNUMBER:
        if (lua_isinteger(state, index) != 0) {
            const lua_Integer value = lua_tointeger(state, index);
            if (value >= std::numeric_limits<int>::min() && value <= std::numeric_limits<int>::max()) {
                return ScriptValue{ static_cast<int>(value) };
            }
            if (value >= 0) {
                return ScriptValue{ static_cast<std::uint64_t>(value), ScriptValueType::Entity };
            }
            return ScriptValue{ static_cast<float>(value) };
        }
        return ScriptValue{ static_cast<float>(lua_tonumber(state, index)) };
    case LUA_TSTRING:
        return ScriptValue{ std::string{ lua_tostring(state, index) } };
    default:
        break;
    }
    return ScriptValue{};
}

void PucLuaValueBridge::Push(lua_State* state, const ScriptValue& value) {
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

ScriptValue PucLuaValueBridge::DefaultFor(ScriptValueType type) {
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

bool PucLuaValueBridge::IsCompatible(ScriptValue value, ScriptValueType expected) noexcept {
    return value.Type() == expected ||
           (expected == ScriptValueType::Float && value.Type() == ScriptValueType::Int) ||
           ((expected == ScriptValueType::Entity || expected == ScriptValueType::Component) &&
               value.Type() == ScriptValueType::Int &&
               value.AsInt() >= 0);
}

ScriptValue PucLuaValueBridge::Coerce(ScriptValue value, ScriptValueType expected) {
    if (expected == ScriptValueType::Float && value.Type() == ScriptValueType::Int) {
        return ScriptValue{ static_cast<float>(value.AsInt()) };
    }
    if ((expected == ScriptValueType::Entity || expected == ScriptValueType::Component) && value.Type() == ScriptValueType::Int) {
        return ScriptValue{ static_cast<std::uint64_t>(value.AsInt()), expected };
    }
    return value;
}

PucLuaExposedVariableInstance* PucLuaValueBridge::FindVariable(std::vector<PucLuaExposedVariableInstance>& variables, std::string_view name) noexcept {
    const auto iter = std::ranges::find_if(variables, [name](const PucLuaExposedVariableInstance& variable) {
        return variable.name == name;
    });
    return iter == variables.end() ? nullptr : &*iter;
}

const PucLuaExposedVariableInstance* PucLuaValueBridge::FindVariable(std::span<const PucLuaExposedVariableInstance> variables, std::string_view name) noexcept {
    const auto iter = std::ranges::find_if(variables, [name](const PucLuaExposedVariableInstance& variable) {
        return variable.name == name;
    });
    return iter == variables.end() ? nullptr : &*iter;
}

} // namespace kb::script
