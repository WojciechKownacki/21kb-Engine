#pragma once

struct lua_State;

namespace kb::script {

class ScriptExecutionContext;
class PucLuaScriptRuntime;

// LIB-105: bespoke Lua attachment for the `Events` table (Subscribe/
// Unsubscribe/Emit/EmitDeferred/Broadcast) — mirrors PucLuaEventApi's
// existing Emit/EmitTo shape (not routed through ScriptFunctionRegistry,
// which has no callback-argument or arbitrary-payload slot; see
// ScriptEventBus.hpp's class comment for why).
class PucLuaEventsApi final {
public:
    static void Attach(lua_State* state, int environmentIndex, ScriptExecutionContext& context, const PucLuaScriptRuntime& runtime);
};

} // namespace kb::script
