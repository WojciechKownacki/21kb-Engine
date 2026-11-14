#pragma once

#include "engine/script/PucLuaScriptRuntime.hpp"

#include <vector>

#include "script/lua/LuaVm.hpp"

namespace kb::script {

class PucLuaDebugHook final {
public:
    static void Install(lua_State* state, const PucLuaScriptRuntime& runtime);
    static void Clear(lua_State* state);
    // The hook function itself: the Lua 5.4 interpreter arms it when an entry has spent its allowance of work.
    [[nodiscard]] static lua_Hook HookFunction() noexcept;
    [[nodiscard]] static PucLuaDebugEvaluationResult Evaluate(lua_State* paused, std::string_view expression, int frame);
    // Frames of `state` from the innermost outwards, with their named locals when `collectLocals` is set. Works
    // on the running thread and on a coroutine suspended at a pause.
    [[nodiscard]] static std::vector<PucLuaDebugFrameSnapshot> CaptureCallStack(lua_State* state, bool collectLocals);
};

} // namespace kb::script
