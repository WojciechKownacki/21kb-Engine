#pragma once

struct lua_State;

namespace kb::script {

class PucLuaSandboxEnvironment final {
public:
    static void OpenSafeLibraries(lua_State* state);
    static void Create(lua_State* state);
};

} // namespace kb::script
