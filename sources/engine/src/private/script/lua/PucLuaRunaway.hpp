#pragma once

#include "script/lua/LuaVm.hpp"

#include <cstddef>
#include <memory>

namespace kb::script {

class PucLuaScriptRuntime;

// The execution budget of one Lua state: it ends an entry that has run past the limit. The limit is the engine's
// "script work per behaviour call" setting, counted as loop iterations and calls by the Lua 5.4 interpreter and as
// processor time by the LuaJIT watchdog (compiled code has no instruction a counter could see). One implementation is
// built, the one for the selected VM.
class PucLuaRunaway final {
public:
    [[nodiscard]] static std::unique_ptr<PucLuaRunaway> Create(lua_State* state, PucLuaScriptRuntime& runtime);
    ~PucLuaRunaway();
    PucLuaRunaway(const PucLuaRunaway&) = delete;
    PucLuaRunaway& operator=(const PucLuaRunaway&) = delete;

    // An entry (or a script's top-level code) starts: `limit` units of work are allowed (zero: no limit).
    void Arm(std::size_t limit) noexcept;
    // The entry ended.
    void Disarm() noexcept;
    // Code that removes the hook of the thread (a nested call returning to its caller) must not lift an interruption of
    // the running entry with it: the entry stays interrupted, so a script cannot get past it by catching the error. The
    // Lua 5.4 interpreter keeps its allowance spent, which arms the hook again at the next loop or call by itself.
    static void Rearm(lua_State* state) noexcept;
    // The count event of the debug hook: the interpreter's budget event (Lua 5.4), which ends or suspends the entry.
    static void OnCountHook(lua_State* state, PucLuaScriptRuntime& runtime, lua_Hook hook);

    struct Impl;

private:
    explicit PucLuaRunaway(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

} // namespace kb::script
