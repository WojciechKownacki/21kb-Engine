#pragma once

#include "engine/script/PucLuaScriptRuntime.hpp"

#include <span>
#include <string_view>
#include <vector>

struct lua_State;

namespace kb::script {

class PucLuaValueBridge final {
public:
    [[nodiscard]] static ScriptValue FromLua(lua_State* state, int index);
    static void Push(lua_State* state, const ScriptValue& value);

    [[nodiscard]] static ScriptValue DefaultFor(ScriptValueType type);
    [[nodiscard]] static bool IsCompatible(ScriptValue value, ScriptValueType expected) noexcept;
    [[nodiscard]] static ScriptValue Coerce(ScriptValue value, ScriptValueType expected);

    [[nodiscard]] static PucLuaExposedVariableInstance* FindVariable(std::vector<PucLuaExposedVariableInstance>& variables, std::string_view name) noexcept;
    [[nodiscard]] static const PucLuaExposedVariableInstance* FindVariable(std::span<const PucLuaExposedVariableInstance> variables, std::string_view name) noexcept;
};

} // namespace kb::script
