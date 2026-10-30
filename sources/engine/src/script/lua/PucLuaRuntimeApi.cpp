#include "script/lua/PucLuaRuntimeApi.hpp"

#include "engine/script/PucLuaScriptRuntime.hpp"
#include "script/lua/PucLuaStateUtilities.hpp"
#include "script/lua/api/PucLuaEventApi.hpp"
#include "script/lua/api/PucLuaEventsApi.hpp"
#include "script/lua/api/PucLuaFunctionApi.hpp"
#include "script/lua/api/PucLuaModuleApi.hpp"
#include "script/lua/api/PucLuaSelfApi.hpp"
#include "script/lua/api/PucLuaSharedApi.hpp"
#include "script/lua/api/PucLuaTaskApi.hpp"

namespace kb::script {

PucLuaStateLifetime* PucLuaRuntimeApi::ContextSlot(lua_State* state) noexcept {
    if (state == nullptr) return nullptr;
    auto* runtime = *static_cast<PucLuaScriptRuntime**>(lua_getextraspace(state));
    return runtime != nullptr ? runtime->stateLifetime_.get() : nullptr;
}

ScriptExecutionContext* PucLuaRuntimeApi::ContextFromUpvalue(lua_State* state) noexcept {
    const auto* slot = static_cast<PucLuaStateLifetime*>(lua_touserdata(state, lua_upvalueindex(1)));
    return slot != nullptr ? slot->activeContext : nullptr;
}

void PucLuaRuntimeApi::PushContextSlot(lua_State* state, bool enabled) {
    lua_pushlightuserdata(state, enabled ? ContextSlot(state) : nullptr);
}

std::optional<std::string> PucLuaRuntimeApi::AttachRuntimeFunctions(lua_State* state, int environmentIndex, PucLuaScriptRuntime& runtime) {
    PucLuaModuleApi::AttachImport(state, environmentIndex, runtime);
    // Attach `Inspector` with a null context so top-level `Inspector.x = default`
    // declarations run harmlessly at chunk load (the schema is parsed statically).
    PucLuaSelfApi::AttachInspector(state, environmentIndex, nullptr);
    return PucLuaTaskApi::Attach(state, environmentIndex);
}

std::optional<std::string> PucLuaRuntimeApi::AttachExecutionApi(lua_State* state, int environmentIndex, ScriptExecutionContext& context, PucLuaScriptRuntime& runtime) {
    if (std::optional<std::string> error = AttachRuntimeFunctions(state, environmentIndex, runtime)) {
        return error;
    }
    PucLuaEventApi::Attach(state, environmentIndex, context);
    PucLuaEventsApi::Attach(state, environmentIndex, context, runtime);
    PucLuaSharedApi::Attach(state, environmentIndex, context);
    PucLuaFunctionApi::Attach(state, environmentIndex, context);
    // Re-attach `Inspector` bound to THIS execution's context so `Inspector.x`
    // read/write routes to the current entity's exposed-variable instance.
    PucLuaSelfApi::AttachInspector(state, environmentIndex, &context);
    return std::nullopt;
}

void PucLuaRuntimeApi::PushSelf(lua_State* state, ScriptExecutionContext& context) {
    PucLuaSelfApi::PushSelf(state, context);
}

void PucLuaRuntimeApi::PushEvent(lua_State* state, const ScriptEvent& event) {
    PucLuaEventApi::PushEvent(state, event);
}

} // namespace kb::script
