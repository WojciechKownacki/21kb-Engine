#include "script/lua/PucLuaDebugHook.hpp"

#include "engine/script/PucLuaScriptRuntime.hpp"
#include "script/lua/PucLuaStateUtilities.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kb::script {
namespace {

[[nodiscard]] bool IsBreakpointMatch(std::string_view configuredChunk, std::string_view actualChunk) noexcept {
    if (configuredChunk.empty()) {
        return true;
    }
    return actualChunk == configuredChunk || actualChunk.ends_with(configuredChunk);
}

[[nodiscard]] PucLuaDebugVariableSnapshot DebugVariable(lua_State* state, const char* name, int valueIndex) {
    PucLuaDebugVariableSnapshot variable{
        .name = name == nullptr ? std::string{} : std::string{ name },
        .value = "<unsupported>",
        .type = ScriptValueType::Void,
    };
    switch (lua_type(state, valueIndex)) {
    case LUA_TBOOLEAN:
        variable.value = lua_toboolean(state, valueIndex) != 0 ? "true" : "false";
        variable.type = ScriptValueType::Bool;
        break;
    case LUA_TNUMBER:
        if (lua_isinteger(state, valueIndex) != 0) {
            variable.value = std::to_string(static_cast<int>(lua_tointeger(state, valueIndex)));
            variable.type = ScriptValueType::Int;
        } else {
            variable.value = std::to_string(static_cast<float>(lua_tonumber(state, valueIndex)));
            variable.type = ScriptValueType::Float;
        }
        break;
    case LUA_TSTRING:
        variable.value = lua_tostring(state, valueIndex);
        variable.type = ScriptValueType::String;
        break;
    case LUA_TNIL:
        variable.value = "nil";
        break;
    default:
        variable.value = luaL_typename(state, valueIndex);
        break;
    }
    return variable;
}

[[nodiscard]] PucLuaDebugPauseSnapshot CapturePause(
    lua_State* state,
    PucLuaDebugPauseReason reason,
    const PucLuaDebugSettings& settings,
    const lua_Debug& currentDebug) {
    PucLuaDebugPauseSnapshot snapshot{
        .valid = true,
        .reason = reason,
        .chunkName = PucLuaErrorReporter::ChunkFromDebug(const_cast<lua_Debug&>(currentDebug)),
        .line = currentDebug.currentline,
    };
    if (settings.collectCallStack) {
        snapshot.callStack = PucLuaDebugHook::CaptureCallStack(state, settings.collectLocals);
    }
    return snapshot;
}

// The actual hook logic, split out from Hook() below so the latter can
// wrap it in a try/catch. Builds std::string/std::vector snapshots
// (CapturePause, ChunkFromDebug) that can in principle throw (std::
// bad_alloc). A pause suspends the coroutine with lua_yield, which from a
// hook only marks the thread and returns here; Lua unwinds after the hook.
void HookImpl(lua_State* state, lua_Debug* debug) {
    auto* runtime = *static_cast<PucLuaScriptRuntime**>(lua_getextraspace(state));
    if (runtime == nullptr || debug == nullptr) {
        return;
    }
    if (debug->event == LUA_HOOKCOUNT &&
        !runtime->ConsumeLuaInstructions(100U)) {
        if (runtime->ExecutionBudgetPolicy() ==
            kb::core::BudgetExceededPolicy::Suspend && lua_isyieldable(state) != 0) {
            static_cast<void>(lua_yieldk(state, 0, 0, nullptr));
            return;
        }
        luaL_error(state, "lua execution budget exceeded");
    }
    // Breakpoints and steps act on line events only, and not while another script is suspended at a pause.
    if (debug->event != LUA_HOOKLINE || runtime->IsDebugPaused()) {
        return;
    }
    lua_getinfo(state, "Sl", debug);
    const std::string chunk = PucLuaErrorReporter::ChunkFromDebug(*debug);
    const PucLuaDebugSettings& settings = runtime->DebugSettings();
    bool shouldPause = false;
    PucLuaDebugPauseReason reason = PucLuaDebugPauseReason::Breakpoint;
    if (const std::optional<PucLuaDebugPauseReason> requested = runtime->ConsumeRequestedDebugPause(state); requested.has_value()) {
        shouldPause = true;
        reason = *requested;
    }
    if (settings.enableBreakpoints) {
        for (const PucLuaDebugBreakpoint& breakpoint : settings.breakpoints) {
            if (!breakpoint.enabled || breakpoint.line != debug->currentline || !IsBreakpointMatch(breakpoint.chunkName, chunk)) {
                continue;
            }
            shouldPause = true;
            reason = PucLuaDebugPauseReason::Breakpoint;
            break;
        }
    }
    if (!shouldPause) {
        return;
    }
    // Only a coroutine can be suspended. Code Lua cannot yield from (a chunk's top-level code run by pcall, or
    // Lua called back from C such as a sort comparator) records the pause and goes on instead of failing.
    PucLuaDebugPauseSnapshot snapshot = CapturePause(state, reason, settings, *debug);
    snapshot.suspended = settings.stopOnBreakpoint && lua_isyieldable(state) != 0 && runtime->BeginDebugSuspend(state);
    const bool suspend = snapshot.suspended;
    runtime->RecordDebugPause(std::move(snapshot));
    if (suspend) {
        static_cast<void>(lua_yield(state, 0));
    }
}

// LIB-010: lua_sethook registers this directly with Lua's bytecode
// interpreter (LUA_MASKLINE — fired on every executed line while a debug
// session is active), entirely outside ScriptFunctionRegistry::Call and
// PucLuaSafeCall's lua_CFunction-shaped trampoline (a hook is
// void(lua_State*, lua_Debug*), not int(lua_State*), so it cannot be
// registered through lua_pushcclosure/PucLuaSafeCall). A C++ exception
// escaping here would need to unwind through the interpreter's own C
// dispatch loop — the identical undefined-behaviour risk PucLuaSafeCall
// closes for every registered function, just reached through Lua's other
// C callback mechanism instead of lua_pcall.
void Hook(lua_State* state, lua_Debug* debug) {
    try {
        HookImpl(state, debug);
    } catch (const std::exception& exception) {
        luaL_error(state, "%s", exception.what());
    } catch (...) {
        luaL_error(state, "kb::script: unknown C++ exception crossed the Lua debug hook");
    }
}

} // namespace

std::vector<PucLuaDebugFrameSnapshot> PucLuaDebugHook::CaptureCallStack(lua_State* state, bool collectLocals) {
    std::vector<PucLuaDebugFrameSnapshot> callStack;
    lua_Debug frame{};
    for (int level = 0; lua_getstack(state, level, &frame) != 0; ++level) {
        lua_getinfo(state, "nSl", &frame);
        PucLuaDebugFrameSnapshot frameSnapshot{
            .name = frame.name == nullptr ? std::string{} : std::string{ frame.name },
            .chunkName = PucLuaErrorReporter::ChunkFromDebug(frame),
            .line = frame.currentline,
        };
        // Reading a local pushes it; a suspended coroutine's frame may have no free slot left, as the debug
        // library also accounts for.
        if (collectLocals && lua_checkstack(state, 1) != 0) {
            for (int localIndex = 1;; ++localIndex) {
                const char* localName = lua_getlocal(state, &frame, localIndex);
                if (localName == nullptr) {
                    break;
                }
                if (localName[0] != '(') {
                    frameSnapshot.locals.push_back(DebugVariable(state, localName, -1));
                }
                lua_pop(state, 1);
            }
        }
        callStack.push_back(std::move(frameSnapshot));
    }
    return callStack;
}

void PucLuaDebugHook::Install(lua_State* state, const PucLuaScriptRuntime& runtime) {
    if (runtime.NeedsDebugLineHook()) {
        const bool budgetActive = runtime.HasActiveExecutionBudget();
        lua_sethook(
            state,
            &Hook,
            budgetActive ? LUA_MASKLINE | LUA_MASKCOUNT : LUA_MASKLINE,
            budgetActive ? 100 : 0);
    } else if (runtime.HasActiveExecutionBudget()) {
        lua_sethook(state, &Hook, LUA_MASKCOUNT, 100);
    }
}

void PucLuaDebugHook::Clear(lua_State* state) {
    lua_sethook(state, nullptr, 0, 0);
}

} // namespace kb::script
