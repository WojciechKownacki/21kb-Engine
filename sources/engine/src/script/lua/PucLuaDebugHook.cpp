#include "script/lua/PucLuaDebugHook.hpp"

#include "engine/script/PucLuaScriptRuntime.hpp"
#include "script/lua/PucLuaStateUtilities.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <optional>
#include <cstdlib>
#include <algorithm>
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

[[nodiscard]] PucLuaDebugVariableSnapshot DebugVariable(lua_State* state, const char* name, int valueIndex, std::size_t textLimit = 4096U) {
    PucLuaDebugVariableSnapshot variable{
        .name = name == nullptr ? std::string{} : std::string{ name }.substr(0U, 512U),
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
            variable.value = std::to_string(static_cast<long long>(lua_tointeger(state, valueIndex)));
            variable.type = ScriptValueType::Int;
        } else {
            variable.value = std::to_string(static_cast<double>(lua_tonumber(state, valueIndex)));
            variable.type = ScriptValueType::Float;
        }
        break;
    case LUA_TSTRING: {
        std::size_t size = 0U;
        const char* text = lua_tolstring(state, valueIndex, &size);
        variable.value.assign(text, std::min(size, textLimit));
        if (size > textLimit) variable.value += "...";
        variable.type = ScriptValueType::String;
        break;
    }
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
void Hook(lua_State* state, lua_Debug* debug);

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
        // A script can catch a hook error with pcall. Once exhausted, check every
        // subsequent instruction so it cannot repeatedly catch the error inside
        // another long-running protected call and keep the host occupied forever.
        lua_sethook(state, &Hook, LUA_MASKCOUNT, 1);
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
    for (int level = 0; level < 32 && lua_getstack(state, level, &frame) != 0; ++level) {
        lua_getinfo(state, "nSl", &frame);
        PucLuaDebugFrameSnapshot frameSnapshot{
            .name = frame.name == nullptr ? std::string{} : std::string{ frame.name },
            .chunkName = PucLuaErrorReporter::ChunkFromDebug(frame),
            .line = frame.currentline,
        };
        // Reading a local pushes it; a suspended coroutine's frame may have no free slot left, as the debug
        // library also accounts for.
        if (collectLocals && lua_checkstack(state, 1) != 0) {
            for (int localIndex = 1; localIndex <= 64; ++localIndex) {
                const char* localName = lua_getlocal(state, &frame, localIndex);
                if (localName == nullptr) {
                    break;
                }
                if (localName[0] != '(') {
                    frameSnapshot.locals.push_back(DebugVariable(state, localName, -1, 512U));
                }
                lua_pop(state, 1);
            }
        }
        callStack.push_back(std::move(frameSnapshot));
    }
    return callStack;
}

namespace {
struct EvaluationMemory { std::size_t used = 0U; };
void* EvaluationAllocate(void* context, void* pointer, std::size_t oldSize, std::size_t newSize) {
    auto& memory = *static_cast<EvaluationMemory*>(context);
    const auto previous = pointer == nullptr ? 0U : oldSize;
    if (newSize == 0U) { std::free(pointer); memory.used -= previous; return nullptr; }
    if (newSize > 1024U * 1024U || memory.used - previous > 1024U * 1024U - newSize) return nullptr;
    auto* result = std::realloc(pointer, newSize);
    if (result != nullptr) memory.used = memory.used - previous + newSize;
    return result;
}
void EvaluationHook(lua_State* state, lua_Debug*) {
    auto* remaining = *static_cast<unsigned**>(lua_getextraspace(state));
    if (*remaining <= 100U) luaL_error(state, "debug expression instruction limit exceeded");
    *remaining -= 100U;
}
struct EvaluationInput { lua_State* paused; const char* source; std::size_t size; int frame; };
// No owning C++ locals: Lua errors unwind this protected callback with longjmp.
int EvaluateProtected(lua_State* state) {
    auto* input = static_cast<EvaluationInput*>(lua_touserdata(state, 1));
    lua_Debug frame{};
    if (!lua_getstack(input->paused, input->frame, &frame)) return luaL_error(state, "debug frame does not exist");
    if (luaL_loadbufferx(state, input->source, input->size, "=debug-expression", "t") != LUA_OK) return lua_error(state);
    lua_newtable(state);
    const auto copyScalar = [&](const char* name) {
        if (name == nullptr || name[0] == '(') return;
        switch (lua_type(input->paused, -1)) {
        case LUA_TBOOLEAN: lua_pushboolean(state, lua_toboolean(input->paused, -1)); break;
        case LUA_TNUMBER:
            if (lua_isinteger(input->paused, -1)) lua_pushinteger(state, lua_tointeger(input->paused, -1));
            else lua_pushnumber(state, lua_tonumber(input->paused, -1));
            break;
        case LUA_TSTRING: {
            std::size_t length = 0U;
            const char* value = lua_tolstring(input->paused, -1, &length);
            if (length > 4096U) return;
            lua_pushlstring(state, value, length);
            break;
        }
        case LUA_TNIL: lua_pushnil(state); break;
        default: return;
        }
        lua_setfield(state, -2, name);
    };
    // Upvalues first so an inner local with the same name wins.
    if (lua_getinfo(input->paused, "f", &frame)) {
        const int function = lua_gettop(input->paused);
        for (int index = 1; index <= 128; ++index) {
            const char* name = lua_getupvalue(input->paused, function, index);
            if (!name) break;
            copyScalar(name);
            lua_pop(input->paused, 1);
        }
        lua_pop(input->paused, 1);
    }
    for (int index = 1; index <= 128; ++index) {
        const char* name = lua_getlocal(input->paused, &frame, index);
        if (!name) break;
        copyScalar(name);
        lua_pop(input->paused, 1);
    }
    if (lua_setupvalue(state, -2, 1) == nullptr) lua_pop(state, 1);
    lua_call(state, 0, 1);
    return 1;
}
} // namespace

PucLuaDebugEvaluationResult PucLuaDebugHook::Evaluate(lua_State* paused, std::string_view expression, int frame) {
    if (expression.empty() || expression.size() > 4096U || frame < 0 || frame > 127)
        return {.error = "expression/frame exceeds debugger limits"};
    if (lua_checkstack(paused, 2) == 0) return {.error = "cannot inspect paused Lua stack"};
    const std::string source = "return (" + std::string{expression} + ")";
    EvaluationMemory memory;
    lua_State* state = lua_newstate(&EvaluationAllocate, &memory);
    if (!state) return {.error = "cannot allocate debug expression state"};
    unsigned remaining = 10000U;
    *static_cast<unsigned**>(lua_getextraspace(state)) = &remaining;
    lua_sethook(state, &EvaluationHook, LUA_MASKCOUNT, 100);
    EvaluationInput input{paused, source.data(), source.size(), frame};
    const int savedTop = lua_gettop(paused);
    lua_pushcfunction(state, &EvaluateProtected);
    lua_pushlightuserdata(state, &input);
    const int status = lua_pcall(state, 1, 1, 0);
    lua_settop(paused, savedTop);
    PucLuaDebugEvaluationResult result;
    if (status == LUA_OK) {
        result.succeeded = true;
        result.value = DebugVariable(state, "result", -1);
        if (result.value.value.size() > 4096U) {
            result.succeeded = false;
            result.value = {};
            result.error = "debug expression result exceeds limit";
        }
    } else {
        const char* error = lua_tostring(state, -1);
        result.error = error == nullptr ? "debug expression failed" : std::string{error}.substr(0U, 4096U);
    }
    lua_close(state);
    return result;
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
