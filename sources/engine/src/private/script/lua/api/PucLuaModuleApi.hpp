#pragma once

struct lua_State;

namespace kb::script {

class PucLuaScriptRuntime;

class PucLuaModuleApi final {
public:
    static void AttachImport(lua_State* state, int environmentIndex, PucLuaScriptRuntime& runtime);
};

} // namespace kb::script
