#pragma once

#include "engine/script/ScriptEvent.hpp"
#include "engine/script/ScriptExecutionContext.hpp"

#include <optional>
#include <memory>
#include <string>

struct lua_State;

namespace kb::script {

class PucLuaScriptRuntime;
struct PucLuaStateLifetime;

class PucLuaRuntimeApi final {
public:
    [[nodiscard]] static std::shared_ptr<PucLuaStateLifetime> Lifetime(lua_State* state) noexcept;
    [[nodiscard]] static PucLuaStateLifetime* ContextSlot(lua_State* state) noexcept;
    [[nodiscard]] static ScriptExecutionContext* ContextFromUpvalue(lua_State* state) noexcept;
    static void PushContextSlot(lua_State* state, bool enabled = true);
    [[nodiscard]] static std::optional<std::string> AttachRuntimeFunctions(lua_State* state, int environmentIndex, PucLuaScriptRuntime& runtime);
    [[nodiscard]] static std::optional<std::string> AttachExecutionApi(lua_State* state, int environmentIndex, ScriptExecutionContext& context, PucLuaScriptRuntime& runtime);
    static void PushSelf(lua_State* state, ScriptExecutionContext& context);
    static void PushEvent(lua_State* state, const ScriptEvent& event);
};

} // namespace kb::script
