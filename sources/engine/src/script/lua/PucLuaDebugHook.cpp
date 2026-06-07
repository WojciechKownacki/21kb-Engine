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
    if (!settings.collectCallStack) {
        return snapshot;
    }

    lua_Debug frame{};
    for (int level = 0; lua_getstack(state, level, &frame) != 0; ++level) {
        lua_getinfo(state, "nSl", &frame);
        PucLuaDebugFrameSnapshot frameSnapshot{
            .name = frame.name == nullptr ? std::string{} : std::string{ frame.name },
            .chunkName = PucLuaErrorReporter::ChunkFromDebug(frame),
            .line = frame.currentline,
        };
        if (settings.collectLocals) {
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
        snapshot.callStack.push_back(std::move(frameSnapshot));
    }
    return snapshot;
}

void Hook(lua_State* state, lua_Debug* debug) {
    auto* runtime = *static_cast<PucLuaScriptRuntime**>(lua_getextraspace(state));
    if (runtime == nullptr || debug == nullptr) {
        return;
    }
    lua_getinfo(state, "Sl", debug);
    const std::string chunk = PucLuaErrorReporter::ChunkFromDebug(*debug);
    const PucLuaDebugSettings& settings = runtime->DebugSettings();
    bool shouldPause = false;
    PucLuaDebugPauseReason reason = PucLuaDebugPauseReason::Breakpoint;
    if (const std::optional<PucLuaDebugPauseReason> requested = runtime->ConsumeRequestedDebugPause(); requested.has_value()) {
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
    runtime->RecordDebugPause(CapturePause(state, reason, settings, *debug));
    if (settings.stopOnBreakpoint) {
        const char* label = "lua breakpoint hit";
        if (reason == PucLuaDebugPauseReason::ManualBreak) {
            label = "lua manual break";
        } else if (reason == PucLuaDebugPauseReason::Step) {
            label = "lua step break";
        }
        luaL_error(state, "%s at %s:%d", label, chunk.c_str(), debug->currentline);
    }
}

} // namespace

void PucLuaDebugHook::Install(lua_State* state, const PucLuaScriptRuntime& runtime) {
    if (runtime.NeedsDebugHook()) {
        lua_sethook(state, &Hook, LUA_MASKLINE, 0);
    }
}

void PucLuaDebugHook::Clear(lua_State* state) {
    lua_sethook(state, nullptr, 0, 0);
}

} // namespace kb::script
