#pragma once

#include "engine/script/ScriptEvent.hpp"

#include <vector>

struct lua_State;

namespace kb::script {

class ScriptExecutionContext;

class PucLuaEventApi final {
public:
    static void Attach(lua_State* state, int environmentIndex, ScriptExecutionContext& context);
    static void PushEvent(lua_State* state, const ScriptEvent& event);
    // LIB-105: promoted out of this file's anonymous namespace so
    // PucLuaEventsApi.cpp (Events.Emit/EmitDeferred/Broadcast) can build a
    // ScriptEvent's arguments from a Lua table the exact same way Emit/
    // EmitTo already do, instead of duplicating the table-walk.
    [[nodiscard]] static std::vector<ScriptEventArgument> ArgumentsFromTable(lua_State* state, int index);
};

} // namespace kb::script
