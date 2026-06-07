#pragma once

struct lua_State;

namespace kb::script {

class ScriptExecutionContext;

class PucLuaFunctionApi final {
public:
    static void Attach(lua_State* state, int environmentIndex, ScriptExecutionContext& context);
};

} // namespace kb::script
