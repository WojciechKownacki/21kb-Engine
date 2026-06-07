#pragma once

#include "engine/script/ScriptEvent.hpp"

struct lua_State;

namespace kb::script {

class ScriptExecutionContext;

class PucLuaEventApi final {
public:
    static void Attach(lua_State* state, int environmentIndex, ScriptExecutionContext& context);
    static void PushEvent(lua_State* state, const ScriptEvent& event);
};

} // namespace kb::script
