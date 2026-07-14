#include "script/lua/api/PucLuaEventsApi.hpp"

#include "engine/script/PucLuaScriptRuntime.hpp"
#include "engine/script/ScriptEventBus.hpp"
#include "engine/script/ScriptExecutionContext.hpp"
#include "script/lua/PucLuaDebugHook.hpp"
#include "script/lua/PucLuaStateUtilities.hpp"
#include "script/lua/api/PucLuaEventApi.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace kb::script {
namespace {

// Owns the LUA_REGISTRYINDEX reference a subscription holds onto the Lua
// callback function value. Kept alive by a shared_ptr captured inside the
// NativeEventSubscriptionCallback closure ScriptEventBus stores — when
// Unsubscribe (or the whole bus/subscription vector) destroys that
// std::function, this drops to zero refs and ~SubscriberInvocation runs,
// releasing the registry slot. Safe only as long as `state` outlives the
// last subscription: kb::script::ScriptRuntimeHostState (ScriptRuntimeHost.
// cpp) declares `luaRuntime` (owns the lua_State) BEFORE `runtime` (owns
// the ScriptEventBus holding these subscriptions), so members are torn down
// in the required order (runtime/subscriptions first, lua_State after) —
// verified by reading that struct before writing this. Test code
// constructing both directly must declare PucLuaScriptRuntime before
// ScriptRuntime for the same reason (the existing test suite already does).
struct SubscriberInvocation {
    lua_State* state = nullptr;
    const PucLuaScriptRuntime* runtime = nullptr;
    int ref = LUA_NOREF;

    // Explicit constructor (not aggregate init) is required here: a
    // declared destructor suppresses the implicit move constructor, so
    // `std::make_shared<SubscriberInvocation>(SubscriberInvocation{...})`
    // would copy-construct the managed object from a temporary and then
    // destroy that temporary — running ~SubscriberInvocation() (and
    // luaL_unref-ing the just-created ref) immediately, before the
    // subscription was ever used. Constructing in place via make_shared's
    // forwarding constructor call avoids the temporary entirely.
    SubscriberInvocation(lua_State* stateArg, const PucLuaScriptRuntime* runtimeArg, int refArg) noexcept
        : state(stateArg)
        , runtime(runtimeArg)
        , ref(refArg) {}

    SubscriberInvocation(const SubscriberInvocation&) = delete;
    SubscriberInvocation& operator=(const SubscriberInvocation&) = delete;

    ~SubscriberInvocation() {
        if (state != nullptr && ref != LUA_NOREF) {
            luaL_unref(state, LUA_REGISTRYINDEX, ref);
        }
    }
};

void InvokeSubscriber(const std::shared_ptr<SubscriberInvocation>& invocation, const ScriptEvent& event) {
    PucLuaStackGuard stack{ invocation->state };
    lua_rawgeti(invocation->state, LUA_REGISTRYINDEX, invocation->ref);
    lua_pushcfunction(invocation->state, &PucLuaErrorReporter::Traceback);
    const int errorHandlerIndex = lua_gettop(invocation->state) - 1;
    lua_insert(invocation->state, errorHandlerIndex);
    PucLuaEventApi::PushEvent(invocation->state, event);
    PucLuaDebugHook::Install(invocation->state, *invocation->runtime);
    const int status = lua_pcall(invocation->state, 1, 0, errorHandlerIndex);
    PucLuaDebugHook::Clear(invocation->state);
    if (status != LUA_OK) {
        // Caught by ScriptEventBus::Emit's try/catch and turned into a
        // ScriptEventDeliveryResult error string — the same resilience
        // NativeScriptBackend::InvokeNativeCallback already gives native
        // subscribers, now extended to Lua ones.
        throw std::runtime_error(PucLuaErrorReporter::ErrorFromTop(invocation->state));
    }
}

[[nodiscard]] ScriptExecutionContext* ContextFromUpvalue(lua_State* state) noexcept {
    return static_cast<ScriptExecutionContext*>(lua_touserdata(state, lua_upvalueindex(1)));
}

[[nodiscard]] kb::scene::SceneEntity OptionalEntityArg(lua_State* state, int index) {
    if (lua_gettop(state) < index || lua_isnoneornil(state, index) != 0) {
        return kb::scene::SceneEntity{};
    }
    const lua_Integer id = luaL_checkinteger(state, index);
    if (id < 0) {
        return kb::scene::SceneEntity{};
    }
    return kb::scene::SceneEntity{ static_cast<std::uint64_t>(id) };
}

int LuaEventsSubscribe(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    const auto* runtime = static_cast<const PucLuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(2)));
    if (context == nullptr || context->Events() == nullptr || runtime == nullptr) {
        lua_pushinteger(state, static_cast<lua_Integer>(kInvalidEventSubscriptionHandle));
        return 1;
    }
    const char* eventName = luaL_checkstring(state, 1);
    luaL_checktype(state, 2, LUA_TFUNCTION);
    const kb::scene::SceneEntity owner = OptionalEntityArg(state, 3);

    lua_pushvalue(state, 2);
    const int ref = luaL_ref(state, LUA_REGISTRYINDEX);
    auto invocation = std::make_shared<SubscriberInvocation>(state, runtime, ref);
    const EventSubscriptionHandle handle = context->Events()->Subscribe(
        eventName,
        [invocation](const ScriptEvent& event) { InvokeSubscriber(invocation, event); },
        owner);
    if (handle == kInvalidEventSubscriptionHandle) {
        luaL_unref(state, LUA_REGISTRYINDEX, ref);
    }
    lua_pushinteger(state, static_cast<lua_Integer>(handle));
    return 1;
}

int LuaEventsUnsubscribe(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    const lua_Integer handleValue = luaL_checkinteger(state, 1);
    const bool unsubscribed = context != nullptr && context->Events() != nullptr && handleValue > 0 &&
        context->Events()->Unsubscribe(static_cast<EventSubscriptionHandle>(handleValue));
    lua_pushboolean(state, unsubscribed ? 1 : 0);
    return 1;
}

[[nodiscard]] ScriptEvent BuildEvent(lua_State* state, ScriptExecutionContext& context, const char* eventName, kb::scene::SceneEntity target) {
    ScriptEvent event{ eventName };
    event.sender = context.Self();
    event.target = target;
    event.senderAsset = context.Asset();
    if (lua_gettop(state) >= 2 && lua_istable(state, 2) != 0) {
        event.arguments = PucLuaEventApi::ArgumentsFromTable(state, 2);
    }
    return event;
}

int LuaEventsEmit(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    const char* eventName = luaL_checkstring(state, 1);
    if (context == nullptr || context->Events() == nullptr) {
        lua_pushinteger(state, 0);
        return 1;
    }
    const kb::scene::SceneEntity target = OptionalEntityArg(state, 3);
    const ScriptEvent event = BuildEvent(state, *context, eventName, target);
    const ScriptEventDeliveryResult result = context->Events()->Emit(context->GetScene(), event, target);
    lua_pushinteger(state, static_cast<lua_Integer>(result.delivered));
    return 1;
}

int LuaEventsBroadcast(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    const char* eventName = luaL_checkstring(state, 1);
    if (context == nullptr || context->Events() == nullptr) {
        lua_pushinteger(state, 0);
        return 1;
    }
    const ScriptEvent event = BuildEvent(state, *context, eventName, kb::scene::SceneEntity{});
    const ScriptEventDeliveryResult result = context->Events()->Broadcast(context->GetScene(), event);
    lua_pushinteger(state, static_cast<lua_Integer>(result.delivered));
    return 1;
}

int LuaEventsEmitDeferred(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    const char* eventName = luaL_checkstring(state, 1);
    if (context == nullptr || context->Events() == nullptr) {
        return 0;
    }
    const kb::scene::SceneEntity target = OptionalEntityArg(state, 3);
    ScriptEvent event = BuildEvent(state, *context, eventName, target);
    context->Events()->EmitDeferred(std::move(event), target);
    return 0;
}

} // namespace

void PucLuaEventsApi::Attach(lua_State* state, int environmentIndex, ScriptExecutionContext& context, const PucLuaScriptRuntime& runtime) {
    lua_createtable(state, 0, 5);
    const int tableIndex = lua_gettop(state);

    lua_pushlightuserdata(state, &context);
    lua_pushlightuserdata(state, const_cast<void*>(static_cast<const void*>(&runtime)));
    lua_pushcclosure(state, &LuaEventsSubscribe, 2);
    lua_setfield(state, tableIndex, "Subscribe");

    lua_pushlightuserdata(state, &context);
    lua_pushcclosure(state, &LuaEventsUnsubscribe, 1);
    lua_setfield(state, tableIndex, "Unsubscribe");

    lua_pushlightuserdata(state, &context);
    lua_pushcclosure(state, &LuaEventsEmit, 1);
    lua_setfield(state, tableIndex, "Emit");

    lua_pushlightuserdata(state, &context);
    lua_pushcclosure(state, &LuaEventsEmitDeferred, 1);
    lua_setfield(state, tableIndex, "EmitDeferred");

    lua_pushlightuserdata(state, &context);
    lua_pushcclosure(state, &LuaEventsBroadcast, 1);
    lua_setfield(state, tableIndex, "Broadcast");

    lua_setfield(state, environmentIndex, "Events");
}

} // namespace kb::script
