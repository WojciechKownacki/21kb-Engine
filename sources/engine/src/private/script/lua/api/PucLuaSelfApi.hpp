#pragma once

struct lua_State;

namespace kb::script {

class ScriptExecutionContext;

class PucLuaSelfApi final {
public:
    static void PushSelf(lua_State* state, ScriptExecutionContext& context);
};

} // namespace kb::script
