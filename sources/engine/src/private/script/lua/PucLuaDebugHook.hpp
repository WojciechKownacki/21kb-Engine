#pragma once

#include "engine/script/PucLuaScriptRuntime.hpp"

#include <vector>

struct lua_State;

namespace kb::script {

class PucLuaDebugHook final {
public:
    static void Install(lua_State* state, const PucLuaScriptRuntime& runtime);
    static void Clear(lua_State* state);
    // Gives the Lua state `limit` loop iterations and calls (zero: no limit) before the hook is armed on the running
    // thread; the hook then fails or suspends the script by the execution budget policy. DisarmRunaway ends the allowance.
    static void ArmRunaway(lua_State* state, std::size_t limit) noexcept;
    static void DisarmRunaway(lua_State* state) noexcept;
    [[nodiscard]] static PucLuaDebugEvaluationResult Evaluate(lua_State* paused, std::string_view expression, int frame);
    // Frames of `state` from the innermost outwards, with their named locals when `collectLocals` is set. Works
    // on the running thread and on a coroutine suspended at a pause.
    [[nodiscard]] static std::vector<PucLuaDebugFrameSnapshot> CaptureCallStack(lua_State* state, bool collectLocals);
};

} // namespace kb::script
