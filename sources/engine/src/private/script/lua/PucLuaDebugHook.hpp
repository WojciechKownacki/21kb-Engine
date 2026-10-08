#pragma once

#include "engine/script/PucLuaScriptRuntime.hpp"

#include <vector>

struct lua_State;

namespace kb::script {

class PucLuaDebugHook final {
public:
    static void Install(lua_State* state, const PucLuaScriptRuntime& runtime);
    static void Clear(lua_State* state);
    // Frames of `state` from the innermost outwards, with their named locals when `collectLocals` is set. Works
    // on the running thread and on a coroutine suspended at a pause.
    [[nodiscard]] static std::vector<PucLuaDebugFrameSnapshot> CaptureCallStack(lua_State* state, bool collectLocals);
};

} // namespace kb::script
