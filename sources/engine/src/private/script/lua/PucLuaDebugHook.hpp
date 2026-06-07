#pragma once

struct lua_State;

namespace kb::script {

class PucLuaScriptRuntime;

class PucLuaDebugHook final {
public:
    static void Install(lua_State* state, const PucLuaScriptRuntime& runtime);
    static void Clear(lua_State* state);
};

} // namespace kb::script
