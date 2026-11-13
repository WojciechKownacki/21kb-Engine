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

#include "engine/script/ScriptFunctionRegistry.hpp"

extern "C" {
#include <lua.h>
}

#include <cstdint>

namespace kb::script {
namespace {

// Registry key of the weak table that records which script environments already carry the execution API and
// for which function registry it was built.
constexpr char kAttachedEnvironmentsKey = 0;

// Identifies the set of registered functions an environment's API tables were generated from. The registry is
// locked before the first dispatch, so its address and size name the set.
[[nodiscard]] lua_Integer FunctionSetStamp(ScriptExecutionContext& context) noexcept {
    const ScriptFunctionRegistry* functions = context.Functions();
    if (functions == nullptr) {
        return 0;
    }
    return static_cast<lua_Integer>(reinterpret_cast<std::uintptr_t>(functions) * 1000003ULL + functions->Functions().size() + 1U);
}

// True when the environment at `absoluteIndex` was already attached for `stamp`. Leaves the stack as it was.
[[nodiscard]] bool IsAttached(lua_State* state, int absoluteIndex, lua_Integer stamp) {
    if (lua_rawgetp(state, LUA_REGISTRYINDEX, &kAttachedEnvironmentsKey) != LUA_TTABLE) {
        lua_pop(state, 1);
        return false;
    }
    lua_pushvalue(state, absoluteIndex);
    lua_rawget(state, -2);
    const bool attached = lua_isinteger(state, -1) != 0 && lua_tointeger(state, -1) == stamp;
    lua_pop(state, 2);
    return attached;
}

void MarkAttached(lua_State* state, int absoluteIndex, lua_Integer stamp) {
    if (lua_rawgetp(state, LUA_REGISTRYINDEX, &kAttachedEnvironmentsKey) != LUA_TTABLE) {
        lua_pop(state, 1);
        lua_createtable(state, 0, 8);
        lua_createtable(state, 0, 1);
        lua_pushliteral(state, "k");
        lua_setfield(state, -2, "__mode");
        lua_setmetatable(state, -2);
        lua_pushvalue(state, -1);
        lua_rawsetp(state, LUA_REGISTRYINDEX, &kAttachedEnvironmentsKey);
    }
    lua_pushvalue(state, absoluteIndex);
    lua_pushinteger(state, stamp);
    lua_rawset(state, -3);
    lua_pop(state, 1);
}

} // namespace

std::shared_ptr<PucLuaStateLifetime> PucLuaRuntimeApi::Lifetime(lua_State* state) noexcept {
    if (state == nullptr) return {};
    auto* runtime = *static_cast<PucLuaScriptRuntime**>(lua_getextraspace(state));
    return runtime != nullptr ? runtime->stateLifetime_ : nullptr;
}

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
    // The functions this attaches read the running context from the shared slot, not from the table they sit in,
    // so one attachment serves every call; an environment is attached again only for another function set.
    const int absoluteEnvironment = lua_absindex(state, environmentIndex);
    const lua_Integer stamp = FunctionSetStamp(context);
    if (IsAttached(state, absoluteEnvironment, stamp)) {
        return std::nullopt;
    }
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
    MarkAttached(state, absoluteEnvironment, stamp);
    return std::nullopt;
}

void PucLuaRuntimeApi::PushSelf(lua_State* state, ScriptExecutionContext& context) {
    PucLuaSelfApi::PushSelf(state, context);
}

void PucLuaRuntimeApi::RefreshSelfVariables(lua_State* state, int selfIndex, ScriptExecutionContext& context) {
    PucLuaSelfApi::RefreshVariables(state, selfIndex, context);
}

void PucLuaRuntimeApi::PushEvent(lua_State* state, const ScriptEvent& event) {
    PucLuaEventApi::PushEvent(state, event);
}

} // namespace kb::script
