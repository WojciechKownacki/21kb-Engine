#pragma once

#include "engine/script/ScriptEvent.hpp"
#include "engine/script/ScriptExecutionContext.hpp"

#include <optional>
#include <string>

struct lua_State;

namespace kb::script {

class PucLuaScriptRuntime;

class PucLuaRuntimeApi final {
public:
    [[nodiscard]] static std::optional<std::string> AttachRuntimeFunctions(lua_State* state, int environmentIndex, PucLuaScriptRuntime& runtime);
    [[nodiscard]] static std::optional<std::string> AttachExecutionApi(lua_State* state, int environmentIndex, ScriptExecutionContext& context, PucLuaScriptRuntime& runtime);
    static void PushSelf(lua_State* state, ScriptExecutionContext& context);
    static void PushEvent(lua_State* state, const ScriptEvent& event);
};

} // namespace kb::script
