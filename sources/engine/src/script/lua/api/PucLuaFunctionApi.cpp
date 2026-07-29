#include "script/lua/api/PucLuaFunctionApi.hpp"

#include "engine/scene/PhysicsBackend.hpp"
#include "engine/script/PucLuaSafeCall.hpp"
#include "engine/script/ScriptExecutionContext.hpp"
#include "engine/script/ScriptValue.hpp"
#include "script/lua/PucLuaValueBridge.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::script {
namespace {

[[nodiscard]] ScriptExecutionContext* ContextFromUpvalue(lua_State* state) noexcept {
    return static_cast<ScriptExecutionContext*>(lua_touserdata(state, lua_upvalueindex(1)));
}

[[nodiscard]] std::vector<ScriptFunctionArgument> ArgumentsFromTable(lua_State* state, int index) {
    std::vector<ScriptFunctionArgument> arguments;
    if (lua_istable(state, index) == 0) {
        return arguments;
    }

    const int absoluteIndex = lua_absindex(state, index);
    lua_pushnil(state);
    while (lua_next(state, absoluteIndex) != 0) {
        if (lua_type(state, -2) == LUA_TSTRING) {
            arguments.push_back(ScriptFunctionArgument{
                .name = lua_tostring(state, -2),
                .value = PucLuaValueBridge::FromLua(state, -1),
            });
        }
        lua_pop(state, 1);
    }
    return arguments;
}

int LuaCallFunction(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    const char* functionName = luaL_checkstring(state, 1);
    std::vector<ScriptFunctionArgument> arguments;
    if (lua_gettop(state) >= 2 && lua_istable(state, 2) != 0) {
        arguments = ArgumentsFromTable(state, 2);
    }
    const ScriptFunctionCallResult result = context->CallFunction(functionName, arguments);
    if (!result.Succeeded()) {
        lua_pushnil(state);
        const std::string error = result.errors.empty() ? "script function call failed" : result.errors.front();
        lua_pushlstring(state, error.data(), error.size());
        return 2;
    }
    if (result.outputs.empty()) {
        lua_pushnil(state);
        return 1;
    }
    if (result.outputs.size() == 1U) {
        PucLuaValueBridge::Push(state, result.outputs.front().value);
        return 1;
    }
    lua_createtable(state, 0, static_cast<int>(result.outputs.size()));
    for (const ScriptFunctionArgument& output : result.outputs) {
        PucLuaValueBridge::Push(state, output.value);
        lua_setfield(state, -2, output.name.c_str());
    }
    return 1;
}

// Log(value): print-style helper. Coerces any value to a string and forwards it
// to the registered "Log" function (the editor routes that to its Console). A
// no-op when no "Log" function is registered (e.g. headless engine hosts).
int LuaLog(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        return 0;
    }
    std::size_t length = 0;
    const char* text = luaL_tolstring(state, 1, &length);
    std::vector<ScriptFunctionArgument> arguments;
    arguments.push_back(ScriptFunctionArgument{
        .name = "message",
        .value = ScriptValue{ std::string{ text != nullptr ? text : "", text != nullptr ? length : std::size_t{ 0 } } },
    });
    if (text != nullptr) {
        lua_pop(state, 1);
    }
    static_cast<void>(context->CallFunction("Log", arguments));
    return 0;
}

int LuaAudioPlay(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }

    std::vector<ScriptFunctionArgument> arguments;
    if (lua_istable(state, 1) != 0) {
        arguments = ArgumentsFromTable(state, 1);
    } else {
        std::size_t length = 0;
        const char* clip = luaL_tolstring(state, 1, &length);
        arguments.push_back(ScriptFunctionArgument{
            .name = "clip",
            .value = ScriptValue{ std::string{ clip != nullptr ? clip : "", clip != nullptr ? length : std::size_t{ 0 } } },
        });
        if (clip != nullptr) {
            lua_pop(state, 1);
        }
        if (lua_gettop(state) >= 2 && lua_istable(state, 2) != 0) {
            std::vector<ScriptFunctionArgument> options = ArgumentsFromTable(state, 2);
            arguments.insert(arguments.end(), options.begin(), options.end());
        }
    }

    const ScriptFunctionCallResult result = context->CallFunction("Audio.Play", arguments);
    if (!result.Succeeded()) {
        lua_pushnil(state);
        const std::string error = result.errors.empty() ? "audio play failed" : result.errors.front();
        lua_pushlstring(state, error.data(), error.size());
        return 2;
    }

    const std::optional<ScriptValue> voice = result.Output("voice");
    if (!voice.has_value()) {
        lua_pushnil(state);
        return 1;
    }
    PucLuaValueBridge::Push(state, *voice);
    return 1;
}

[[nodiscard]] int PushCallError(lua_State* state, const ScriptFunctionCallResult& result, std::string_view fallback);

// LIB-147: Audio.SetMixer(mixer) -> assigned (boolean, nil+error on an unresolvable/
// wrong-type mixer asset; an empty/absent argument clears back to the implicit master) -
// mirrors LuaPostProcessSetProfile's exact shape.
int LuaAudioSetMixer(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    std::vector<ScriptFunctionArgument> arguments;
    if (lua_gettop(state) >= 1 && lua_isnil(state, 1) == 0) {
        std::size_t length = 0;
        const char* mixer = luaL_tolstring(state, 1, &length);
        arguments.push_back(ScriptFunctionArgument{
            .name = "mixer",
            .value = ScriptValue{ std::string{ mixer != nullptr ? mixer : "", mixer != nullptr ? length : std::size_t{ 0 } } },
        });
        if (mixer != nullptr) {
            lua_pop(state, 1);
        }
    }
    const ScriptFunctionCallResult result = context->CallFunction("Audio.SetMixer", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "audio mixer assignment failed");
    }
    lua_pushboolean(state, result.Output("assigned").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaAudioActiveMixer(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushliteral(state, "");
        return 1;
    }
    const ScriptFunctionCallResult result = context->CallFunction("Audio.ActiveMixer", {});
    const std::string mixer = result.Output("mixer").value_or(ScriptValue{ std::string{} }).AsString();
    lua_pushlstring(state, mixer.data(), mixer.size());
    return 1;
}

// LIB-147: Audio.SetSnapshot(snapshot) -> applied (boolean, nil+error when no mixer is
// active or the name is not declared by it; empty/absent resets to authored volumes).
int LuaAudioSetSnapshot(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    std::vector<ScriptFunctionArgument> arguments;
    if (lua_gettop(state) >= 1 && lua_isnil(state, 1) == 0) {
        std::size_t length = 0;
        const char* snapshot = luaL_tolstring(state, 1, &length);
        arguments.push_back(ScriptFunctionArgument{
            .name = "snapshot",
            .value = ScriptValue{ std::string{ snapshot != nullptr ? snapshot : "", snapshot != nullptr ? length : std::size_t{ 0 } } },
        });
        if (snapshot != nullptr) {
            lua_pop(state, 1);
        }
    }
    const ScriptFunctionCallResult result = context->CallFunction("Audio.SetSnapshot", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "audio snapshot request failed");
    }
    lua_pushboolean(state, result.Output("applied").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

[[nodiscard]] ScriptFunctionArgument Arg(std::string name, ScriptValue value);

// LIB-148: the eight per-voice wrappers share one shape - voice handle at index 1, the
// operation value (when any) at index 2, one boolean back (honest false for a dead voice).
int LuaAudioVoiceCall(lua_State* state, const char* function, const char* resultPin, const char* valuePin, bool valueIsBool) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    std::vector<ScriptFunctionArgument> arguments{
        Arg("voice", ScriptValue{ static_cast<int>(luaL_checkinteger(state, 1)) }),
    };
    if (valuePin != nullptr) {
        if (valueIsBool) {
            arguments.push_back(Arg(valuePin, ScriptValue{ lua_toboolean(state, 2) != 0 }));
        } else {
            arguments.push_back(Arg(valuePin, ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }));
        }
    }
    const ScriptFunctionCallResult result = context->CallFunction(function, arguments);
    lua_pushboolean(state, result.Output(resultPin).value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaAudioStop(lua_State* state) { return LuaAudioVoiceCall(state, "Audio.Stop", "stopped", nullptr, false); }
int LuaAudioPause(lua_State* state) { return LuaAudioVoiceCall(state, "Audio.Pause", "paused", nullptr, false); }
int LuaAudioResume(lua_State* state) { return LuaAudioVoiceCall(state, "Audio.Resume", "resumed", nullptr, false); }
int LuaAudioSeek(lua_State* state) { return LuaAudioVoiceCall(state, "Audio.Seek", "applied", "positionSeconds", false); }
int LuaAudioSetVolume(lua_State* state) { return LuaAudioVoiceCall(state, "Audio.SetVolume", "applied", "volume", false); }
int LuaAudioSetPitch(lua_State* state) { return LuaAudioVoiceCall(state, "Audio.SetPitch", "applied", "pitch", false); }
int LuaAudioSetLoop(lua_State* state) { return LuaAudioVoiceCall(state, "Audio.SetLoop", "applied", "loop", true); }
int LuaAudioIsPlaying(lua_State* state) { return LuaAudioVoiceCall(state, "Audio.IsPlaying", "playing", nullptr, false); }

int LuaAudioActiveSnapshot(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushliteral(state, "");
        return 1;
    }
    const ScriptFunctionCallResult result = context->CallFunction("Audio.ActiveSnapshot", {});
    const std::string snapshot = result.Output("snapshot").value_or(ScriptValue{ std::string{} }).AsString();
    lua_pushlstring(state, snapshot.data(), snapshot.size());
    return 1;
}

// LIB-150: Audio.SetBusVolume(bus, volume) -> applied (nil+error for an undeclared bus or
// no active mixer) - the same resolve-then-single-value-or-nil+error shape as SetSnapshot.
int LuaAudioSetBusVolume(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    std::size_t length = 0;
    const char* bus = luaL_tolstring(state, 1, &length);
    std::vector<ScriptFunctionArgument> arguments{
        Arg("bus", ScriptValue{ std::string{ bus != nullptr ? bus : "", bus != nullptr ? length : std::size_t{ 0 } } }),
    };
    if (bus != nullptr) {
        lua_pop(state, 1);
    }
    arguments.push_back(Arg("volume", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }));
    const ScriptFunctionCallResult result = context->CallFunction("Audio.SetBusVolume", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "audio bus volume request failed");
    }
    lua_pushboolean(state, result.Output("applied").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaAudioClearBusVolume(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    std::size_t length = 0;
    const char* bus = luaL_tolstring(state, 1, &length);
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("bus", ScriptValue{ std::string{ bus != nullptr ? bus : "", bus != nullptr ? length : std::size_t{ 0 } } }),
    };
    if (bus != nullptr) {
        lua_pop(state, 1);
    }
    const ScriptFunctionCallResult result = context->CallFunction("Audio.ClearBusVolume", arguments);
    lua_pushboolean(state, result.Output("cleared").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

// LIB-151: Audio.ConfigureOcclusion(enabled [, options{occludedVolume, maxDistance,
// layerMask, maxRaycastsPerTick}]) -> applied - omitted options keep the current values.
int LuaAudioConfigureOcclusion(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    std::vector<ScriptFunctionArgument> arguments{
        Arg("enabled", ScriptValue{ lua_toboolean(state, 1) != 0 }),
    };
    if (lua_gettop(state) >= 2 && lua_istable(state, 2) != 0) {
        std::vector<ScriptFunctionArgument> options = ArgumentsFromTable(state, 2);
        arguments.insert(arguments.end(), options.begin(), options.end());
    }
    const ScriptFunctionCallResult result = context->CallFunction("Audio.ConfigureOcclusion", arguments);
    lua_pushboolean(state, result.Output("applied").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

// LIB-152: Audio.GetPosition(voice) -> {valid, seconds} - the audio-clock playback
// position table (mirror LuaPhysicsGetVelocity's shape).
int LuaAudioGetPosition(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("voice", ScriptValue{ static_cast<int>(luaL_checkinteger(state, 1)) }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("Audio.GetPosition", arguments);
    lua_createtable(state, 0, 2);
    for (const ScriptFunctionArgument& output : result.outputs) {
        PucLuaValueBridge::Push(state, output.value);
        lua_setfield(state, -2, output.name.c_str());
    }
    return 1;
}

// LIB-152: Audio.AddMarker(voice, marker, positionSeconds) -> added (nil+error for an
// empty marker name).
int LuaAudioAddMarker(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    std::vector<ScriptFunctionArgument> arguments{
        Arg("voice", ScriptValue{ static_cast<int>(luaL_checkinteger(state, 1)) }),
    };
    std::size_t length = 0;
    const char* marker = luaL_tolstring(state, 2, &length);
    arguments.push_back(Arg("marker", ScriptValue{ std::string{ marker != nullptr ? marker : "", marker != nullptr ? length : std::size_t{ 0 } } }));
    if (marker != nullptr) {
        lua_pop(state, 1);
    }
    arguments.push_back(Arg("positionSeconds", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }));
    const ScriptFunctionCallResult result = context->CallFunction("Audio.AddMarker", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "audio marker request failed");
    }
    lua_pushboolean(state, result.Output("added").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaAudioOcclusionEnabled(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const ScriptFunctionCallResult result = context->CallFunction("Audio.OcclusionEnabled", {});
    lua_pushboolean(state, result.Output("enabled").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

// LIB-150: Audio.TransitionToSnapshot(snapshot, durationSeconds) -> started (nil+error for
// an undeclared snapshot or no active mixer; nil/"" snapshot = back to authored volumes).
int LuaAudioTransitionToSnapshot(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    std::vector<ScriptFunctionArgument> arguments;
    if (lua_gettop(state) >= 1 && lua_isnil(state, 1) == 0) {
        std::size_t length = 0;
        const char* snapshot = luaL_tolstring(state, 1, &length);
        arguments.push_back(Arg("snapshot", ScriptValue{ std::string{ snapshot != nullptr ? snapshot : "", snapshot != nullptr ? length : std::size_t{ 0 } } }));
        if (snapshot != nullptr) {
            lua_pop(state, 1);
        }
    }
    arguments.push_back(Arg("durationSeconds", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }));
    const ScriptFunctionCallResult result = context->CallFunction("Audio.TransitionToSnapshot", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "audio snapshot transition request failed");
    }
    lua_pushboolean(state, result.Output("started").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

[[nodiscard]] int PushCallError(lua_State* state, const ScriptFunctionCallResult& result, std::string_view fallback) {
    lua_pushnil(state);
    const std::string error = result.errors.empty() ? std::string{ fallback } : result.errors.front();
    lua_pushlstring(state, error.data(), error.size());
    return 2;
}

[[nodiscard]] ScriptFunctionArgument Arg(std::string name, ScriptValue value) {
    return ScriptFunctionArgument{ std::move(name), std::move(value) };
}

// LIB-137: shared shape for MeshRenderer.SetMesh/SetMaterial - a single required asset
// reference (virtual path or numeric/hex id string, resolved server-side exactly like
// Audio.Play's "clip" argument) plus an optional {entity=...} table, mirroring LuaAudioPlay's
// exact string-or-table-first-arg / optional-table-second-arg shape.
int LuaMeshRendererAssign(lua_State* state, const char* functionName, const char* assetArgumentName) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }

    std::vector<ScriptFunctionArgument> arguments;
    if (lua_istable(state, 1) != 0) {
        arguments = ArgumentsFromTable(state, 1);
    } else {
        std::size_t length = 0;
        const char* asset = luaL_tolstring(state, 1, &length);
        arguments.push_back(ScriptFunctionArgument{
            .name = assetArgumentName,
            .value = ScriptValue{ std::string{ asset != nullptr ? asset : "", asset != nullptr ? length : std::size_t{ 0 } } },
        });
        if (asset != nullptr) {
            lua_pop(state, 1);
        }
        if (lua_gettop(state) >= 2 && lua_istable(state, 2) != 0) {
            std::vector<ScriptFunctionArgument> options = ArgumentsFromTable(state, 2);
            arguments.insert(arguments.end(), options.begin(), options.end());
        }
    }

    const ScriptFunctionCallResult result = context->CallFunction(functionName, arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "mesh renderer assignment failed");
    }
    lua_pushboolean(state, result.Output("assigned").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaMeshRendererSetMesh(lua_State* state) {
    return LuaMeshRendererAssign(state, "MeshRenderer.SetMesh", "mesh");
}

// LIB-138: MeshRenderer.SetMaterialSlot(slot, material, {entity=...}) - a leading integer
// slot index ahead of the same asset-reference-plus-optional-options shape LuaMeshRendererAssign
// already uses for SetMesh/SetMaterial.
int LuaMeshRendererSetMaterialSlot(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }

    const int slot = static_cast<int>(luaL_checkinteger(state, 1));
    std::size_t length = 0;
    const char* material = luaL_tolstring(state, 2, &length);
    std::vector<ScriptFunctionArgument> arguments{
        Arg("slot", ScriptValue{ slot }),
        ScriptFunctionArgument{
            .name = "material",
            .value = ScriptValue{ std::string{ material != nullptr ? material : "", material != nullptr ? length : std::size_t{ 0 } } },
        },
    };
    if (material != nullptr) {
        lua_pop(state, 1);
    }
    if (lua_gettop(state) >= 3 && lua_istable(state, 3) != 0) {
        std::vector<ScriptFunctionArgument> options = ArgumentsFromTable(state, 3);
        arguments.insert(arguments.end(), options.begin(), options.end());
    }

    const ScriptFunctionCallResult result = context->CallFunction("MeshRenderer.SetMaterialSlot", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "mesh renderer material slot assignment failed");
    }
    lua_pushboolean(state, result.Output("assigned").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaMeshRendererGetMaterialSlot(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }

    const int slot = static_cast<int>(luaL_checkinteger(state, 1));
    std::vector<ScriptFunctionArgument> arguments{ Arg("slot", ScriptValue{ slot }) };
    if (lua_gettop(state) >= 2 && lua_istable(state, 2) != 0) {
        std::vector<ScriptFunctionArgument> options = ArgumentsFromTable(state, 2);
        arguments.insert(arguments.end(), options.begin(), options.end());
    }

    const ScriptFunctionCallResult result = context->CallFunction("MeshRenderer.GetMaterialSlot", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "mesh renderer material slot query failed");
    }
    lua_createtable(state, 0, 2);
    for (const ScriptFunctionArgument& output : result.outputs) {
        PucLuaValueBridge::Push(state, output.value);
        lua_setfield(state, -2, output.name.c_str());
    }
    return 1;
}

int LuaMeshRendererClearMaterialSlot(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }

    const int slot = static_cast<int>(luaL_checkinteger(state, 1));
    std::vector<ScriptFunctionArgument> arguments{ Arg("slot", ScriptValue{ slot }) };
    if (lua_gettop(state) >= 2 && lua_istable(state, 2) != 0) {
        std::vector<ScriptFunctionArgument> options = ArgumentsFromTable(state, 2);
        arguments.insert(arguments.end(), options.begin(), options.end());
    }

    const ScriptFunctionCallResult result = context->CallFunction("MeshRenderer.ClearMaterialSlot", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "mesh renderer material slot clear failed");
    }
    lua_pushboolean(state, result.Output("cleared").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

// LIB-139: MeshRenderer.SetMaterialInstance(instance, {entity=...}) - a leading integer
// instance handle (see LuaMaterialInstanceCreate below) plus the same optional-options-table
// shape every other MeshRenderer wrapper already uses.
int LuaMeshRendererSetMaterialInstance(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }

    const auto instance = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    std::vector<ScriptFunctionArgument> arguments{ Arg("instance", ScriptValue{ instance, ScriptValueType::Hash }) };
    if (lua_gettop(state) >= 2 && lua_istable(state, 2) != 0) {
        std::vector<ScriptFunctionArgument> options = ArgumentsFromTable(state, 2);
        arguments.insert(arguments.end(), options.begin(), options.end());
    }

    const ScriptFunctionCallResult result = context->CallFunction("MeshRenderer.SetMaterialInstance", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "mesh renderer material instance assignment failed");
    }
    lua_pushboolean(state, result.Output("assigned").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaMeshRendererClearMaterialInstance(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }

    std::vector<ScriptFunctionArgument> arguments;
    if (lua_gettop(state) >= 1 && lua_istable(state, 1) != 0) {
        arguments = ArgumentsFromTable(state, 1);
    }

    const ScriptFunctionCallResult result = context->CallFunction("MeshRenderer.ClearMaterialInstance", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "mesh renderer material instance clear failed");
    }
    lua_pushboolean(state, result.Output("cleared").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

// LIB-139: MaterialInstance.Create(material) -> instance handle (an integer, nil+error on
// an unresolvable/wrong-type parent or a full instance table) - mirrors LuaAudioPlay's
// resolve-then-single-value-or-nil+error shape exactly.
int LuaMaterialInstanceCreate(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }

    std::size_t length = 0;
    const char* material = luaL_tolstring(state, 1, &length);
    std::vector<ScriptFunctionArgument> arguments{
        ScriptFunctionArgument{
            .name = "material",
            .value = ScriptValue{ std::string{ material != nullptr ? material : "", material != nullptr ? length : std::size_t{ 0 } } },
        },
    };
    if (material != nullptr) {
        lua_pop(state, 1);
    }

    const ScriptFunctionCallResult result = context->CallFunction("MaterialInstance.Create", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "material instance create failed");
    }
    PucLuaValueBridge::Push(state, result.Output("instance").value_or(ScriptValue{ 0U, ScriptValueType::Hash }));
    return 1;
}

int LuaMaterialInstanceRelease(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const auto instance = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    const std::vector<ScriptFunctionArgument> arguments{ Arg("instance", ScriptValue{ instance, ScriptValueType::Hash }) };
    const ScriptFunctionCallResult result = context->CallFunction("MaterialInstance.Release", arguments);
    lua_pushboolean(state, result.Output("released").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaMaterialInstanceExists(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const auto instance = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    const std::vector<ScriptFunctionArgument> arguments{ Arg("instance", ScriptValue{ instance, ScriptValueType::Hash }) };
    const ScriptFunctionCallResult result = context->CallFunction("MaterialInstance.Exists", arguments);
    lua_pushboolean(state, result.Output("exists").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaMaterialInstanceParent(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushliteral(state, "");
        return 1;
    }
    const auto instance = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    const std::vector<ScriptFunctionArgument> arguments{ Arg("instance", ScriptValue{ instance, ScriptValueType::Hash }) };
    const ScriptFunctionCallResult result = context->CallFunction("MaterialInstance.Parent", arguments);
    const std::string material = result.Output("material").value_or(ScriptValue{ std::string{} }).AsString();
    lua_pushlstring(state, material.data(), material.size());
    return 1;
}

// LIB-140: MaterialInstance.SetParameterScalar(instance, name, value) -> applied (boolean).
int LuaMaterialInstanceSetParameterScalar(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const auto instance = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    const char* name = luaL_checkstring(state, 2);
    const auto value = static_cast<float>(luaL_checknumber(state, 3));
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("instance", ScriptValue{ instance, ScriptValueType::Hash }),
        Arg("name", ScriptValue{ std::string{ name } }),
        Arg("value", ScriptValue{ value }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("MaterialInstance.SetParameterScalar", arguments);
    lua_pushboolean(state, result.Output("applied").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

// LIB-140: MaterialInstance.SetParameterBool(instance, name, value) -> applied (boolean).
int LuaMaterialInstanceSetParameterBool(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const auto instance = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    const char* name = luaL_checkstring(state, 2);
    const bool value = lua_toboolean(state, 3) != 0;
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("instance", ScriptValue{ instance, ScriptValueType::Hash }),
        Arg("name", ScriptValue{ std::string{ name } }),
        Arg("value", ScriptValue{ value }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("MaterialInstance.SetParameterBool", arguments);
    lua_pushboolean(state, result.Output("applied").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

// LIB-140: MaterialInstance.ClearParameter(instance, name) -> cleared (boolean).
int LuaMaterialInstanceClearParameter(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const auto instance = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    const char* name = luaL_checkstring(state, 2);
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("instance", ScriptValue{ instance, ScriptValueType::Hash }),
        Arg("name", ScriptValue{ std::string{ name } }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("MaterialInstance.ClearParameter", arguments);
    lua_pushboolean(state, result.Output("cleared").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

// LIB-143: Particles.Create(effect, {entity=...}) -> instance handle (an integer, nil+error
// on an unresolvable/wrong-type effect, unresolvable material reference, dead/missing owner
// entity, or a full instance table) - mirrors LuaMaterialInstanceCreate's exact
// resolve-then-single-value-or-nil+error shape, plus the optional-entity-table convention
// every MeshRenderer wrapper already uses (entity defaults to self when omitted).
int LuaParticlesCreate(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }

    std::size_t length = 0;
    const char* effect = luaL_tolstring(state, 1, &length);
    std::vector<ScriptFunctionArgument> arguments{
        ScriptFunctionArgument{
            .name = "effect",
            .value = ScriptValue{ std::string{ effect != nullptr ? effect : "", effect != nullptr ? length : std::size_t{ 0 } } },
        },
    };
    if (effect != nullptr) {
        lua_pop(state, 1);
    }
    if (lua_gettop(state) >= 2 && lua_istable(state, 2) != 0) {
        std::vector<ScriptFunctionArgument> options = ArgumentsFromTable(state, 2);
        arguments.insert(arguments.end(), options.begin(), options.end());
    }

    const ScriptFunctionCallResult result = context->CallFunction("Particles.Create", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "particle system create failed");
    }
    PucLuaValueBridge::Push(state, result.Output("instance").value_or(ScriptValue{ 0U, ScriptValueType::Hash }));
    return 1;
}

int LuaParticlesRelease(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const auto instance = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    const std::vector<ScriptFunctionArgument> arguments{ Arg("instance", ScriptValue{ instance, ScriptValueType::Hash }) };
    const ScriptFunctionCallResult result = context->CallFunction("Particles.Release", arguments);
    lua_pushboolean(state, result.Output("released").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaParticlesExists(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const auto instance = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    const std::vector<ScriptFunctionArgument> arguments{ Arg("instance", ScriptValue{ instance, ScriptValueType::Hash }) };
    const ScriptFunctionCallResult result = context->CallFunction("Particles.Exists", arguments);
    lua_pushboolean(state, result.Output("exists").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaParticlesPlay(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const auto instance = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    const std::vector<ScriptFunctionArgument> arguments{ Arg("instance", ScriptValue{ instance, ScriptValueType::Hash }) };
    const ScriptFunctionCallResult result = context->CallFunction("Particles.Play", arguments);
    lua_pushboolean(state, result.Output("set").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaParticlesStop(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const auto instance = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    const std::vector<ScriptFunctionArgument> arguments{ Arg("instance", ScriptValue{ instance, ScriptValueType::Hash }) };
    const ScriptFunctionCallResult result = context->CallFunction("Particles.Stop", arguments);
    lua_pushboolean(state, result.Output("set").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaParticlesIsPlaying(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const auto instance = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    const std::vector<ScriptFunctionArgument> arguments{ Arg("instance", ScriptValue{ instance, ScriptValueType::Hash }) };
    const ScriptFunctionCallResult result = context->CallFunction("Particles.IsPlaying", arguments);
    lua_pushboolean(state, result.Output("playing").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaParticlesSetSeed(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const auto instance = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    const auto seed = static_cast<std::uint64_t>(luaL_checkinteger(state, 2));
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("instance", ScriptValue{ instance, ScriptValueType::Hash }),
        Arg("seed", ScriptValue{ seed, ScriptValueType::Hash }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("Particles.SetSeed", arguments);
    lua_pushboolean(state, result.Output("set").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

// LIB-143: Particles.SetParameterScalar(instance, name, value) -> applied (boolean).
int LuaParticlesSetParameterScalar(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const auto instance = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    const char* name = luaL_checkstring(state, 2);
    const auto value = static_cast<float>(luaL_checknumber(state, 3));
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("instance", ScriptValue{ instance, ScriptValueType::Hash }),
        Arg("name", ScriptValue{ std::string{ name } }),
        Arg("value", ScriptValue{ value }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("Particles.SetParameterScalar", arguments);
    lua_pushboolean(state, result.Output("applied").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaParticlesClearParameter(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const auto instance = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    const char* name = luaL_checkstring(state, 2);
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("instance", ScriptValue{ instance, ScriptValueType::Hash }),
        Arg("name", ScriptValue{ std::string{ name } }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("Particles.ClearParameter", arguments);
    lua_pushboolean(state, result.Output("cleared").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

// LIB-143: Particles.Emit(instance, count) -> emitted (boolean) - the ticket's "event" verb,
// an immediate on-demand burst independent of Play/Stop state.
int LuaParticlesEmit(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const auto instance = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    const auto count = static_cast<int>(luaL_checkinteger(state, 2));
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("instance", ScriptValue{ instance, ScriptValueType::Hash }),
        Arg("count", ScriptValue{ count }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("Particles.Emit", arguments);
    lua_pushboolean(state, result.Output("emitted").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaParticlesLiveCount(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushinteger(state, 0);
        return 1;
    }
    const auto instance = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    const std::vector<ScriptFunctionArgument> arguments{ Arg("instance", ScriptValue{ instance, ScriptValueType::Hash }) };
    const ScriptFunctionCallResult result = context->CallFunction("Particles.LiveCount", arguments);
    lua_pushinteger(state, result.Output("count").value_or(ScriptValue{ 0 }).AsInt());
    return 1;
}

// LIB-142: PostProcess.SetProfile(profile) -> assigned (boolean, nil+error on an
// unresolvable/wrong-type profile asset) - mirrors LuaMeshRendererAssign's exact
// resolve-then-single-value-or-nil+error shape.
int LuaPostProcessSetProfile(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }

    std::size_t length = 0;
    const char* profile = luaL_tolstring(state, 1, &length);
    std::vector<ScriptFunctionArgument> arguments{
        ScriptFunctionArgument{
            .name = "profile",
            .value = ScriptValue{ std::string{ profile != nullptr ? profile : "", profile != nullptr ? length : std::size_t{ 0 } } },
        },
    };
    if (profile != nullptr) {
        lua_pop(state, 1);
    }

    const ScriptFunctionCallResult result = context->CallFunction("PostProcess.SetProfile", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "post process profile assignment failed");
    }
    lua_pushboolean(state, result.Output("assigned").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaPostProcessClearProfile(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const ScriptFunctionCallResult result = context->CallFunction("PostProcess.ClearProfile", {});
    lua_pushboolean(state, result.Output("cleared").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaPostProcessActiveProfile(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushliteral(state, "");
        return 1;
    }
    const ScriptFunctionCallResult result = context->CallFunction("PostProcess.ActiveProfile", {});
    const std::string profile = result.Output("profile").value_or(ScriptValue{ std::string{} }).AsString();
    lua_pushlstring(state, profile.data(), profile.size());
    return 1;
}

// LIB-144: Renderer.IsVisible([options{entity=...}]) -> visible (boolean, nil+error for a
// dead entity) - entity defaults to self, the same optional-entity-table convention every
// MeshRenderer wrapper already uses.
int LuaRendererIsVisible(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    std::vector<ScriptFunctionArgument> arguments{};
    if (lua_gettop(state) >= 1 && lua_istable(state, 1) != 0) {
        arguments = ArgumentsFromTable(state, 1);
    }
    const ScriptFunctionCallResult result = context->CallFunction("Renderer.IsVisible", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "renderer visibility query failed");
    }
    lua_pushboolean(state, result.Output("visible").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

// LIB-144: Renderer.GetBounds([options{entity=...}]) -> {found, centerX, centerY, centerZ,
// radius} (nil+error for a dead entity) - mirrors LuaPhysicsGetVelocity's exact
// every-output-as-table-field shape.
int LuaRendererGetBounds(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    std::vector<ScriptFunctionArgument> arguments{};
    if (lua_gettop(state) >= 1 && lua_istable(state, 1) != 0) {
        arguments = ArgumentsFromTable(state, 1);
    }
    const ScriptFunctionCallResult result = context->CallFunction("Renderer.GetBounds", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "renderer bounds query failed");
    }
    lua_createtable(state, 0, 5);
    for (const ScriptFunctionArgument& output : result.outputs) {
        PucLuaValueBridge::Push(state, output.value);
        lua_setfield(state, -2, output.name.c_str());
    }
    return 1;
}

// LIB-144: Renderer.TestFrustum(centerX, centerY, centerZ [, radius]) -> inside (boolean,
// fail-closed false before any visibility frame was published - see
// SceneRenderFeedback.hpp's contract).
int LuaRendererTestFrustum(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("centerX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 1)) }),
        Arg("centerY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
        Arg("centerZ", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
        Arg("radius", ScriptValue{ static_cast<float>(luaL_optnumber(state, 4, 0.0)) }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("Renderer.TestFrustum", arguments);
    lua_pushboolean(state, result.Output("inside").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaRendererHasFrame(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const ScriptFunctionCallResult result = context->CallFunction("Renderer.HasFrame", {});
    lua_pushboolean(state, result.Output("published").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

// LIB-145: Renderer.WorldToScreen(x, y, z) -> {valid, onScreen, screenX, screenY, depth} -
// mirrors LuaPhysicsGetVelocity's every-output-as-table-field shape.
int LuaRendererWorldToScreen(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("x", ScriptValue{ static_cast<float>(luaL_checknumber(state, 1)) }),
        Arg("y", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
        Arg("z", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("Renderer.WorldToScreen", arguments);
    lua_createtable(state, 0, 5);
    for (const ScriptFunctionArgument& output : result.outputs) {
        PucLuaValueBridge::Push(state, output.value);
        lua_setfield(state, -2, output.name.c_str());
    }
    return 1;
}

// LIB-145: Renderer.ScreenPointToRay(screenX, screenY) -> {valid, originX/Y/Z,
// directionX/Y/Z} - the outputs feed Physics.Raycast's pins directly.
int LuaRendererScreenPointToRay(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("screenX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 1)) }),
        Arg("screenY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("Renderer.ScreenPointToRay", arguments);
    lua_createtable(state, 0, 7);
    for (const ScriptFunctionArgument& output : result.outputs) {
        PucLuaValueBridge::Push(state, output.value);
        lua_setfield(state, -2, output.name.c_str());
    }
    return 1;
}

int LuaRendererScreenToWorld(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("screenX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 1)) }),
        Arg("screenY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
        Arg("distance", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("Renderer.ScreenToWorld", arguments);
    lua_createtable(state, 0, 4);
    for (const ScriptFunctionArgument& output : result.outputs) {
        PucLuaValueBridge::Push(state, output.value);
        lua_setfield(state, -2, output.name.c_str());
    }
    return 1;
}

// LIB-145: Renderer.CaptureScreen(path) -> capture id (nil+error when the path is empty or
// another capture is still pending) - mirrors LuaParticlesCreate's
// single-value-or-nil+error shape.
int LuaRendererCaptureScreen(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    std::size_t length = 0;
    const char* path = luaL_tolstring(state, 1, &length);
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("path", ScriptValue{ std::string{ path != nullptr ? path : "", path != nullptr ? length : std::size_t{ 0 } } }),
    };
    if (path != nullptr) {
        lua_pop(state, 1);
    }
    const ScriptFunctionCallResult result = context->CallFunction("Renderer.CaptureScreen", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "screen capture request failed");
    }
    PucLuaValueBridge::Push(state, result.Output("capture").value_or(ScriptValue{ 0U, ScriptValueType::Hash }));
    return 1;
}

int LuaRendererCaptureStatus(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushliteral(state, "unknown");
        return 1;
    }
    const auto capture = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    const std::vector<ScriptFunctionArgument> arguments{ Arg("capture", ScriptValue{ capture, ScriptValueType::Hash }) };
    const ScriptFunctionCallResult result = context->CallFunction("Renderer.CaptureStatus", arguments);
    const std::string status = result.Output("status").value_or(ScriptValue{ std::string{ "unknown" } }).AsString();
    lua_pushlstring(state, status.data(), status.size());
    return 1;
}

int LuaMeshRendererSetMaterial(lua_State* state) {
    return LuaMeshRendererAssign(state, "MeshRenderer.SetMaterial", "material");
}

int LuaWorldFindByName(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    const char* name = luaL_checkstring(state, 1);
    std::vector<ScriptFunctionArgument> arguments{ Arg("name", ScriptValue{ std::string{ name != nullptr ? name : "" } }) };
    const ScriptFunctionCallResult result = context->CallFunction("World.FindByName", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "world find failed");
    }
    PucLuaValueBridge::Push(state, result.Output("entity").value_or(ScriptValue{ 0U, ScriptValueType::Entity }));
    return 1;
}

int LuaWorldExists(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const auto entity = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    std::vector<ScriptFunctionArgument> arguments{ Arg("entity", ScriptValue{ entity, ScriptValueType::Entity }) };
    const ScriptFunctionCallResult result = context->CallFunction("World.Exists", arguments);
    lua_pushboolean(state, result.Output("exists").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaWorldName(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushliteral(state, "");
        return 1;
    }
    const auto entity = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    std::vector<ScriptFunctionArgument> arguments{ Arg("entity", ScriptValue{ entity, ScriptValueType::Entity }) };
    const ScriptFunctionCallResult result = context->CallFunction("World.Name", arguments);
    const std::string name = result.Output("name").value_or(ScriptValue{ std::string{} }).AsString();
    lua_pushlstring(state, name.data(), name.size());
    return 1;
}

int LuaWorldSpawn(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    std::vector<ScriptFunctionArgument> arguments;
    if (lua_istable(state, 1) != 0) {
        arguments = ArgumentsFromTable(state, 1);
    } else if (lua_gettop(state) >= 1 && lua_isnil(state, 1) == 0) {
        std::size_t length = 0;
        const char* name = luaL_tolstring(state, 1, &length);
        arguments.push_back(Arg("name", ScriptValue{ std::string{ name != nullptr ? name : "", name != nullptr ? length : std::size_t{ 0 } } }));
        if (name != nullptr) {
            lua_pop(state, 1);
        }
    }
    const ScriptFunctionCallResult result = context->CallFunction("World.Spawn", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "world spawn failed");
    }
    PucLuaValueBridge::Push(state, result.Output("entity").value_or(ScriptValue{ 0U, ScriptValueType::Entity }));
    return 1;
}

int LuaWorldDestroy(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const auto entity = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    std::vector<ScriptFunctionArgument> arguments{ Arg("entity", ScriptValue{ entity, ScriptValueType::Entity }) };
    const ScriptFunctionCallResult result = context->CallFunction("World.Destroy", arguments);
    lua_pushboolean(state, result.Output("destroyed").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaWorldSetTag(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const auto entity = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    const char* tag = luaL_checkstring(state, 2);
    const bool enabled = lua_gettop(state) < 3 || lua_toboolean(state, 3) != 0;
    std::vector<ScriptFunctionArgument> arguments{
        Arg("entity", ScriptValue{ entity, ScriptValueType::Entity }),
        Arg("tag", ScriptValue{ std::string{ tag != nullptr ? tag : "" } }),
        Arg("enabled", ScriptValue{ enabled }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("World.SetTag", arguments);
    lua_pushboolean(state, result.Output("tagged").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaWorldHasTag(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const auto entity = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    const char* tag = luaL_checkstring(state, 2);
    std::vector<ScriptFunctionArgument> arguments{
        Arg("entity", ScriptValue{ entity, ScriptValueType::Entity }),
        Arg("tag", ScriptValue{ std::string{ tag != nullptr ? tag : "" } }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("World.HasTag", arguments);
    lua_pushboolean(state, result.Output("tagged").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaWorldFindByTag(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    const char* tag = luaL_checkstring(state, 1);
    std::vector<ScriptFunctionArgument> arguments{ Arg("tag", ScriptValue{ std::string{ tag != nullptr ? tag : "" } }) };
    const ScriptFunctionCallResult result = context->CallFunction("World.FindByTag", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "world find by tag failed");
    }
    PucLuaValueBridge::Push(state, result.Output("entity").value_or(ScriptValue{ 0U, ScriptValueType::Entity }));
    return 1;
}

int LuaWorldSetParent(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const auto entity = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    const auto parent = static_cast<std::uint64_t>(luaL_optinteger(state, 2, 0));
    std::vector<ScriptFunctionArgument> arguments{
        Arg("entity", ScriptValue{ entity, ScriptValueType::Entity }),
        Arg("parent", ScriptValue{ parent, ScriptValueType::Entity }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("World.SetParent", arguments);
    lua_pushboolean(state, result.Output("parented").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaWorldInstantiatePrefab(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    std::vector<ScriptFunctionArgument> arguments;
    if (lua_istable(state, 1) != 0) {
        arguments = ArgumentsFromTable(state, 1);
    } else {
        const char* prefab = luaL_checkstring(state, 1);
        arguments.push_back(Arg("prefab", ScriptValue{ std::string{ prefab != nullptr ? prefab : "" } }));
    }
    const ScriptFunctionCallResult result = context->CallFunction("World.InstantiatePrefab", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "prefab instantiate failed");
    }
    PucLuaValueBridge::Push(state, result.Output("entity").value_or(ScriptValue{ 0U, ScriptValueType::Entity }));
    return 1;
}

int LuaSceneLoad(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    const char* path = luaL_checkstring(state, 1);
    const bool additive = lua_gettop(state) >= 2 && lua_toboolean(state, 2) != 0;
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("path", ScriptValue{ std::string{ path != nullptr ? path : "" } }),
        Arg("additive", ScriptValue{ additive }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("Scene.Load", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "scene load failed");
    }
    PucLuaValueBridge::Push(state, result.Output("id").value_or(ScriptValue{ 0U, ScriptValueType::Hash }));
    return 1;
}

int LuaSceneUnload(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const auto id = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("id", ScriptValue{ id, ScriptValueType::Hash }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("Scene.Unload", arguments);
    lua_pushboolean(state, result.Output("unloaded").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaSceneSetActive(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const auto id = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("id", ScriptValue{ id, ScriptValueType::Hash }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("Scene.SetActive", arguments);
    lua_pushboolean(state, result.Output("set").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaSceneGetActive(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushinteger(state, 0);
        return 1;
    }
    const ScriptFunctionCallResult result = context->CallFunction("Scene.GetActive", {});
    PucLuaValueBridge::Push(state, result.Output("id").value_or(ScriptValue{ 0U, ScriptValueType::Hash }));
    return 1;
}

int LuaSceneFind(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushinteger(state, 0);
        return 1;
    }
    const char* name = luaL_checkstring(state, 1);
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("name", ScriptValue{ std::string{ name != nullptr ? name : "" } }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("Scene.Find", arguments);
    PucLuaValueBridge::Push(state, result.Output("id").value_or(ScriptValue{ 0U, ScriptValueType::Hash }));
    return 1;
}

int LuaSceneLoadProgress(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnumber(state, 0.0);
        return 1;
    }
    const auto id = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("id", ScriptValue{ id, ScriptValueType::Hash }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("Scene.LoadProgress", arguments);
    lua_pushnumber(state, static_cast<lua_Number>(result.Output("progress").value_or(ScriptValue{ 0.0F }).AsFloat()));
    return 1;
}

int LuaTransformGetPosition(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    const auto entity = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    std::vector<ScriptFunctionArgument> arguments{ Arg("entity", ScriptValue{ entity, ScriptValueType::Entity }) };
    const ScriptFunctionCallResult result = context->CallFunction("Transform.GetPosition", arguments);
    if (!result.Succeeded() || !result.Output("found").value_or(ScriptValue{ false }).AsBool()) {
        lua_pushnil(state);
        return 1;
    }

    lua_createtable(state, 0, 3);
    lua_pushnumber(state, static_cast<lua_Number>(result.Output("x").value_or(ScriptValue{ 0.0F }).AsFloat()));
    lua_setfield(state, -2, "x");
    lua_pushnumber(state, static_cast<lua_Number>(result.Output("y").value_or(ScriptValue{ 0.0F }).AsFloat()));
    lua_setfield(state, -2, "y");
    lua_pushnumber(state, static_cast<lua_Number>(result.Output("z").value_or(ScriptValue{ 0.0F }).AsFloat()));
    lua_setfield(state, -2, "z");
    return 1;
}

int LuaTransformSetPosition(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    std::vector<ScriptFunctionArgument> arguments;
    if (lua_istable(state, 1) != 0) {
        arguments = ArgumentsFromTable(state, 1);
    } else {
        arguments = {
            Arg("entity", ScriptValue{ static_cast<std::uint64_t>(luaL_checkinteger(state, 1)), ScriptValueType::Entity }),
            Arg("x", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
            Arg("y", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
            Arg("z", ScriptValue{ static_cast<float>(luaL_checknumber(state, 4)) }),
        };
    }
    const ScriptFunctionCallResult result = context->CallFunction("Transform.SetPosition", arguments);
    lua_pushboolean(state, result.Output("moved").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaTransformTranslate(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    std::vector<ScriptFunctionArgument> arguments;
    if (lua_istable(state, 1) != 0) {
        arguments = ArgumentsFromTable(state, 1);
    } else {
        arguments = {
            Arg("entity", ScriptValue{ static_cast<std::uint64_t>(luaL_checkinteger(state, 1)), ScriptValueType::Entity }),
            Arg("x", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
            Arg("y", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
            Arg("z", ScriptValue{ static_cast<float>(luaL_checknumber(state, 4)) }),
        };
    }
    const ScriptFunctionCallResult result = context->CallFunction("Transform.Translate", arguments);
    lua_pushboolean(state, result.Output("moved").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaTimeDelta(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnumber(state, 0.0);
        return 1;
    }
    const ScriptFunctionCallResult result = context->CallFunction("Time.Delta", {});
    lua_pushnumber(state, static_cast<lua_Number>(result.Output("delta").value_or(ScriptValue{ 0.0F }).AsFloat()));
    return 1;
}

int LuaPhysicsRaycast(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    std::vector<ScriptFunctionArgument> arguments;
    if (lua_istable(state, 1) != 0) {
        arguments = ArgumentsFromTable(state, 1);
    } else {
        arguments = {
            Arg("originX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 1)) }),
            Arg("originY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
            Arg("originZ", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
            Arg("directionX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 4)) }),
            Arg("directionY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 5)) }),
            Arg("directionZ", ScriptValue{ static_cast<float>(luaL_checknumber(state, 6)) }),
            Arg("distance", ScriptValue{ static_cast<float>(luaL_optnumber(state, 7, 1000.0)) }),
            Arg("layerMask", ScriptValue{ static_cast<int>(luaL_optinteger(state, 8, static_cast<int>(kb::scene::kPhysicsAllLayers))) }),
        };
    }
    const ScriptFunctionCallResult result = context->CallFunction("Physics.Raycast", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "physics raycast failed");
    }
    lua_createtable(state, 0, 9);
    for (const ScriptFunctionArgument& output : result.outputs) {
        PucLuaValueBridge::Push(state, output.value);
        lua_setfield(state, -2, output.name.c_str());
    }
    return 1;
}

// LIB-124: entity is ALWAYS read via luaL_checkinteger and explicitly tagged
// ScriptValueType::Entity here - NOT through the generic table/
// ArgumentsFromTable path other wrappers above also support, which infers a
// Lua integer's ScriptValueType purely from its own magnitude
// (PucLuaValueBridge::FromLua: only becomes Entity-typed once the value
// exceeds int32 range) and would silently mis-marshal a small-magnitude
// entity id as Int instead (see LIB-123's World.SetPropertyEntity notes).
[[nodiscard]] std::uint64_t CheckEntityArg(lua_State* state, int index) {
    return static_cast<std::uint64_t>(luaL_checkinteger(state, index));
}

int LuaPhysicsAddForce(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("entity", ScriptValue{ CheckEntityArg(state, 1), ScriptValueType::Entity }),
        Arg("forceX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
        Arg("forceY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
        Arg("forceZ", ScriptValue{ static_cast<float>(luaL_checknumber(state, 4)) }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("Physics.AddForce", arguments);
    lua_pushboolean(state, result.Output("applied").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaPhysicsAddImpulse(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("entity", ScriptValue{ CheckEntityArg(state, 1), ScriptValueType::Entity }),
        Arg("impulseX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
        Arg("impulseY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
        Arg("impulseZ", ScriptValue{ static_cast<float>(luaL_checknumber(state, 4)) }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("Physics.AddImpulse", arguments);
    lua_pushboolean(state, result.Output("applied").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaPhysicsSetVelocity(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("entity", ScriptValue{ CheckEntityArg(state, 1), ScriptValueType::Entity }),
        Arg("velocityX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
        Arg("velocityY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
        Arg("velocityZ", ScriptValue{ static_cast<float>(luaL_checknumber(state, 4)) }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("Physics.SetVelocity", arguments);
    lua_pushboolean(state, result.Output("applied").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaPhysicsGetVelocity(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    const std::vector<ScriptFunctionArgument> arguments{ Arg("entity", ScriptValue{ CheckEntityArg(state, 1), ScriptValueType::Entity }) };
    const ScriptFunctionCallResult result = context->CallFunction("Physics.GetVelocity", arguments);
    lua_createtable(state, 0, 4);
    for (const ScriptFunctionArgument& output : result.outputs) {
        PucLuaValueBridge::Push(state, output.value);
        lua_setfield(state, -2, output.name.c_str());
    }
    return 1;
}

int LuaPhysicsSetAngularVelocity(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("entity", ScriptValue{ CheckEntityArg(state, 1), ScriptValueType::Entity }),
        Arg("angularVelocityX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
        Arg("angularVelocityY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
        Arg("angularVelocityZ", ScriptValue{ static_cast<float>(luaL_checknumber(state, 4)) }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("Physics.SetAngularVelocity", arguments);
    lua_pushboolean(state, result.Output("applied").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaPhysicsGetAngularVelocity(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    const std::vector<ScriptFunctionArgument> arguments{ Arg("entity", ScriptValue{ CheckEntityArg(state, 1), ScriptValueType::Entity }) };
    const ScriptFunctionCallResult result = context->CallFunction("Physics.GetAngularVelocity", arguments);
    lua_createtable(state, 0, 4);
    for (const ScriptFunctionArgument& output : result.outputs) {
        PucLuaValueBridge::Push(state, output.value);
        lua_setfield(state, -2, output.name.c_str());
    }
    return 1;
}

int LuaPhysicsMoveKinematic(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("entity", ScriptValue{ CheckEntityArg(state, 1), ScriptValueType::Entity }),
        Arg("targetX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
        Arg("targetY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
        Arg("targetZ", ScriptValue{ static_cast<float>(luaL_checknumber(state, 4)) }),
        Arg("rotationX", ScriptValue{ static_cast<float>(luaL_optnumber(state, 5, 0.0)) }),
        Arg("rotationY", ScriptValue{ static_cast<float>(luaL_optnumber(state, 6, 0.0)) }),
        Arg("rotationZ", ScriptValue{ static_cast<float>(luaL_optnumber(state, 7, 0.0)) }),
        Arg("rotationW", ScriptValue{ static_cast<float>(luaL_optnumber(state, 8, 1.0)) }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("Physics.MoveKinematic", arguments);
    lua_pushboolean(state, result.Output("applied").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaPhysicsSleep(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const std::vector<ScriptFunctionArgument> arguments{ Arg("entity", ScriptValue{ CheckEntityArg(state, 1), ScriptValueType::Entity }) };
    const ScriptFunctionCallResult result = context->CallFunction("Physics.Sleep", arguments);
    lua_pushboolean(state, result.Output("applied").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaPhysicsWake(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const std::vector<ScriptFunctionArgument> arguments{ Arg("entity", ScriptValue{ CheckEntityArg(state, 1), ScriptValueType::Entity }) };
    const ScriptFunctionCallResult result = context->CallFunction("Physics.Wake", arguments);
    lua_pushboolean(state, result.Output("applied").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaPhysicsIsSleeping(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const std::vector<ScriptFunctionArgument> arguments{ Arg("entity", ScriptValue{ CheckEntityArg(state, 1), ScriptValueType::Entity }) };
    const ScriptFunctionCallResult result = context->CallFunction("Physics.IsSleeping", arguments);
    lua_pushboolean(state, result.Output("sleeping").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

// LIB-129: Physics.LayerBit("Enemy") -> integer bit value, ready to OR
// (Lua's `|` since 5.3) into any layerMask argument above.
int LuaPhysicsLayerBit(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushinteger(state, 0);
        return 1;
    }
    const char* name = luaL_checkstring(state, 1);
    const std::vector<ScriptFunctionArgument> arguments{ Arg("name", ScriptValue{ std::string{ name != nullptr ? name : "" } }) };
    const ScriptFunctionCallResult result = context->CallFunction("Physics.LayerBit", arguments);
    lua_pushinteger(state, static_cast<lua_Integer>(result.Output("bit").value_or(ScriptValue{ 0 }).AsInt()));
    return 1;
}

// LIB-131: Physics.CharacterMove(entity, moveX, moveZ) -> bool. Only two positional numeric
// args (no table form needed - unlike Cast/Overlap below, this always takes an entity so
// mirrors AddForce/SetVelocity's CheckEntityArg pattern).
int LuaPhysicsCharacterMove(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("entity", ScriptValue{ CheckEntityArg(state, 1), ScriptValueType::Entity }),
        Arg("moveX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
        Arg("moveZ", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("Physics.CharacterMove", arguments);
    lua_pushboolean(state, result.Output("applied").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaPhysicsCharacterJump(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("entity", ScriptValue{ CheckEntityArg(state, 1), ScriptValueType::Entity }),
        Arg("speed", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("Physics.CharacterJump", arguments);
    lua_pushboolean(state, result.Output("applied").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaPhysicsCharacterVelocity(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    const std::vector<ScriptFunctionArgument> arguments{ Arg("entity", ScriptValue{ CheckEntityArg(state, 1), ScriptValueType::Entity }) };
    const ScriptFunctionCallResult result = context->CallFunction("Physics.CharacterVelocity", arguments);
    lua_createtable(state, 0, 4);
    for (const ScriptFunctionArgument& output : result.outputs) {
        PucLuaValueBridge::Push(state, output.value);
        lua_setfield(state, -2, output.name.c_str());
    }
    return 1;
}

int LuaPhysicsCharacterIsGrounded(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const std::vector<ScriptFunctionArgument> arguments{ Arg("entity", ScriptValue{ CheckEntityArg(state, 1), ScriptValueType::Entity }) };
    const ScriptFunctionCallResult result = context->CallFunction("Physics.CharacterIsGrounded", arguments);
    lua_pushboolean(state, result.Output("grounded").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaPhysicsCharacterGroundNormal(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    const std::vector<ScriptFunctionArgument> arguments{ Arg("entity", ScriptValue{ CheckEntityArg(state, 1), ScriptValueType::Entity }) };
    const ScriptFunctionCallResult result = context->CallFunction("Physics.CharacterGroundNormal", arguments);
    lua_createtable(state, 0, 4);
    for (const ScriptFunctionArgument& output : result.outputs) {
        PucLuaValueBridge::Push(state, output.value);
        lua_setfield(state, -2, output.name.c_str());
    }
    return 1;
}

int LuaPhysicsCharacterGroundVelocity(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    const std::vector<ScriptFunctionArgument> arguments{ Arg("entity", ScriptValue{ CheckEntityArg(state, 1), ScriptValueType::Entity }) };
    const ScriptFunctionCallResult result = context->CallFunction("Physics.CharacterGroundVelocity", arguments);
    lua_createtable(state, 0, 4);
    for (const ScriptFunctionArgument& output : result.outputs) {
        PucLuaValueBridge::Push(state, output.value);
        lua_setfield(state, -2, output.name.c_str());
    }
    return 1;
}

// LIB-132: Physics.SetDebugDrawEnabled(bool) / Physics.IsDebugDrawEnabled() -> bool. No
// entity argument, so a plain lua_toboolean/lua_pushboolean pair suffices.
int LuaPhysicsSetDebugDrawEnabled(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        return 0;
    }
    const std::vector<ScriptFunctionArgument> arguments{ Arg("enabled", ScriptValue{ lua_toboolean(state, 1) != 0 }) };
    static_cast<void>(context->CallFunction("Physics.SetDebugDrawEnabled", arguments));
    return 0;
}

int LuaPhysicsIsDebugDrawEnabled(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const ScriptFunctionCallResult result = context->CallFunction("Physics.IsDebugDrawEnabled", {});
    lua_pushboolean(state, result.Output("enabled").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

// LIB-125: SphereCast/BoxCast/CapsuleCast/OverlapSphere/OverlapBox/
// OverlapCapsule take no entity argument, so (unlike the CheckEntityArg
// functions above) the generic ArgumentsFromTable path is safe here - there
// is no entity id for it to silently mis-marshal. Table-or-positional
// mirrors Raycast above; positional fallbacks match the native functions'
// own optional-pin defaults (Physics.SphereCast etc. in ScriptPhysicsApi.cpp).
[[nodiscard]] int PushShapeQueryLayerMaskDefault() noexcept {
    return static_cast<int>(kb::scene::kPhysicsAllLayers);
}

int LuaPhysicsSphereCast(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    std::vector<ScriptFunctionArgument> arguments;
    if (lua_istable(state, 1) != 0) {
        arguments = ArgumentsFromTable(state, 1);
    } else {
        arguments = {
            Arg("originX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 1)) }),
            Arg("originY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
            Arg("originZ", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
            Arg("directionX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 4)) }),
            Arg("directionY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 5)) }),
            Arg("directionZ", ScriptValue{ static_cast<float>(luaL_checknumber(state, 6)) }),
            Arg("distance", ScriptValue{ static_cast<float>(luaL_optnumber(state, 7, 1000.0)) }),
            Arg("radius", ScriptValue{ static_cast<float>(luaL_optnumber(state, 8, 0.5)) }),
            Arg("layerMask", ScriptValue{ static_cast<int>(luaL_optinteger(state, 9, PushShapeQueryLayerMaskDefault())) }),
        };
    }
    const ScriptFunctionCallResult result = context->CallFunction("Physics.SphereCast", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "physics sphere cast failed");
    }
    lua_createtable(state, 0, 9);
    for (const ScriptFunctionArgument& output : result.outputs) {
        PucLuaValueBridge::Push(state, output.value);
        lua_setfield(state, -2, output.name.c_str());
    }
    return 1;
}

int LuaPhysicsBoxCast(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    std::vector<ScriptFunctionArgument> arguments;
    if (lua_istable(state, 1) != 0) {
        arguments = ArgumentsFromTable(state, 1);
    } else {
        arguments = {
            Arg("originX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 1)) }),
            Arg("originY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
            Arg("originZ", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
            Arg("directionX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 4)) }),
            Arg("directionY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 5)) }),
            Arg("directionZ", ScriptValue{ static_cast<float>(luaL_checknumber(state, 6)) }),
            Arg("distance", ScriptValue{ static_cast<float>(luaL_optnumber(state, 7, 1000.0)) }),
            Arg("halfExtentsX", ScriptValue{ static_cast<float>(luaL_optnumber(state, 8, 0.5)) }),
            Arg("halfExtentsY", ScriptValue{ static_cast<float>(luaL_optnumber(state, 9, 0.5)) }),
            Arg("halfExtentsZ", ScriptValue{ static_cast<float>(luaL_optnumber(state, 10, 0.5)) }),
            Arg("layerMask", ScriptValue{ static_cast<int>(luaL_optinteger(state, 11, PushShapeQueryLayerMaskDefault())) }),
        };
    }
    const ScriptFunctionCallResult result = context->CallFunction("Physics.BoxCast", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "physics box cast failed");
    }
    lua_createtable(state, 0, 9);
    for (const ScriptFunctionArgument& output : result.outputs) {
        PucLuaValueBridge::Push(state, output.value);
        lua_setfield(state, -2, output.name.c_str());
    }
    return 1;
}

int LuaPhysicsCapsuleCast(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    std::vector<ScriptFunctionArgument> arguments;
    if (lua_istable(state, 1) != 0) {
        arguments = ArgumentsFromTable(state, 1);
    } else {
        arguments = {
            Arg("originX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 1)) }),
            Arg("originY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
            Arg("originZ", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
            Arg("directionX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 4)) }),
            Arg("directionY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 5)) }),
            Arg("directionZ", ScriptValue{ static_cast<float>(luaL_checknumber(state, 6)) }),
            Arg("distance", ScriptValue{ static_cast<float>(luaL_optnumber(state, 7, 1000.0)) }),
            Arg("radius", ScriptValue{ static_cast<float>(luaL_optnumber(state, 8, 0.5)) }),
            Arg("height", ScriptValue{ static_cast<float>(luaL_optnumber(state, 9, 2.0)) }),
            Arg("layerMask", ScriptValue{ static_cast<int>(luaL_optinteger(state, 10, PushShapeQueryLayerMaskDefault())) }),
        };
    }
    const ScriptFunctionCallResult result = context->CallFunction("Physics.CapsuleCast", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "physics capsule cast failed");
    }
    lua_createtable(state, 0, 9);
    for (const ScriptFunctionArgument& output : result.outputs) {
        PucLuaValueBridge::Push(state, output.value);
        lua_setfield(state, -2, output.name.c_str());
    }
    return 1;
}

int LuaPhysicsOverlapSphere(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    std::vector<ScriptFunctionArgument> arguments;
    if (lua_istable(state, 1) != 0) {
        arguments = ArgumentsFromTable(state, 1);
    } else {
        arguments = {
            Arg("centerX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 1)) }),
            Arg("centerY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
            Arg("centerZ", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
            Arg("radius", ScriptValue{ static_cast<float>(luaL_optnumber(state, 4, 0.5)) }),
            Arg("layerMask", ScriptValue{ static_cast<int>(luaL_optinteger(state, 5, PushShapeQueryLayerMaskDefault())) }),
        };
    }
    const ScriptFunctionCallResult result = context->CallFunction("Physics.OverlapSphere", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "physics overlap sphere failed");
    }
    lua_createtable(state, 0, 2);
    for (const ScriptFunctionArgument& output : result.outputs) {
        PucLuaValueBridge::Push(state, output.value);
        lua_setfield(state, -2, output.name.c_str());
    }
    return 1;
}

int LuaPhysicsOverlapBox(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    std::vector<ScriptFunctionArgument> arguments;
    if (lua_istable(state, 1) != 0) {
        arguments = ArgumentsFromTable(state, 1);
    } else {
        arguments = {
            Arg("centerX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 1)) }),
            Arg("centerY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
            Arg("centerZ", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
            Arg("halfExtentsX", ScriptValue{ static_cast<float>(luaL_optnumber(state, 4, 0.5)) }),
            Arg("halfExtentsY", ScriptValue{ static_cast<float>(luaL_optnumber(state, 5, 0.5)) }),
            Arg("halfExtentsZ", ScriptValue{ static_cast<float>(luaL_optnumber(state, 6, 0.5)) }),
            Arg("layerMask", ScriptValue{ static_cast<int>(luaL_optinteger(state, 7, PushShapeQueryLayerMaskDefault())) }),
        };
    }
    const ScriptFunctionCallResult result = context->CallFunction("Physics.OverlapBox", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "physics overlap box failed");
    }
    lua_createtable(state, 0, 2);
    for (const ScriptFunctionArgument& output : result.outputs) {
        PucLuaValueBridge::Push(state, output.value);
        lua_setfield(state, -2, output.name.c_str());
    }
    return 1;
}

int LuaPhysicsOverlapCapsule(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    std::vector<ScriptFunctionArgument> arguments;
    if (lua_istable(state, 1) != 0) {
        arguments = ArgumentsFromTable(state, 1);
    } else {
        arguments = {
            Arg("centerX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 1)) }),
            Arg("centerY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
            Arg("centerZ", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
            Arg("radius", ScriptValue{ static_cast<float>(luaL_optnumber(state, 4, 0.5)) }),
            Arg("height", ScriptValue{ static_cast<float>(luaL_optnumber(state, 5, 2.0)) }),
            Arg("layerMask", ScriptValue{ static_cast<int>(luaL_optinteger(state, 6, PushShapeQueryLayerMaskDefault())) }),
        };
    }
    const ScriptFunctionCallResult result = context->CallFunction("Physics.OverlapCapsule", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "physics overlap capsule failed");
    }
    lua_createtable(state, 0, 2);
    for (const ScriptFunctionArgument& output : result.outputs) {
        PucLuaValueBridge::Push(state, output.value);
        lua_setfield(state, -2, output.name.c_str());
    }
    return 1;
}

int LuaPhysicsClosestPoint(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("entity", ScriptValue{ CheckEntityArg(state, 1), ScriptValueType::Entity }),
        Arg("pointX", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
        Arg("pointY", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
        Arg("pointZ", ScriptValue{ static_cast<float>(luaL_checknumber(state, 4)) }),
        Arg("layerMask", ScriptValue{ static_cast<int>(luaL_optinteger(state, 5, PushShapeQueryLayerMaskDefault())) }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("Physics.ClosestPoint", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "physics closest point failed");
    }
    lua_createtable(state, 0, 5);
    for (const ScriptFunctionArgument& output : result.outputs) {
        PucLuaValueBridge::Push(state, output.value);
        lua_setfield(state, -2, output.name.c_str());
    }
    return 1;
}

[[nodiscard]] std::vector<ScriptFunctionArgument> InputActionArgs(lua_State* state) {
    const char* action = luaL_checkstring(state, 1);
    std::vector<ScriptFunctionArgument> arguments{ Arg("action", ScriptValue{ std::string{ action != nullptr ? action : "" } }) };
    const int player = static_cast<int>(luaL_optinteger(state, 2, 0));
    if (player > 0) {
        arguments.push_back(Arg("player", ScriptValue{ player }));
    }
    return arguments;
}

int LuaInputBool(lua_State* state, std::string_view functionName, std::string_view outputName) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const ScriptFunctionCallResult result = context->CallFunction(functionName, InputActionArgs(state));
    lua_pushboolean(state, result.Output(outputName).value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaInputIsPressed(lua_State* state) {
    return LuaInputBool(state, "Input.IsPressed", "pressed");
}

int LuaInputWasPressed(lua_State* state) {
    return LuaInputBool(state, "Input.WasPressed", "pressed");
}

int LuaInputWasReleased(lua_State* state) {
    return LuaInputBool(state, "Input.WasReleased", "released");
}

int LuaInputHeld(lua_State* state) {
    return LuaInputBool(state, "Input.Held", "held");
}

int LuaInputPressed(lua_State* state) {
    return LuaInputBool(state, "Input.Pressed", "pressed");
}

int LuaInputReleased(lua_State* state) {
    return LuaInputBool(state, "Input.Released", "released");
}

int LuaInputActionBool(lua_State* state) {
    return LuaInputBool(state, "Input.ActionBool", "value");
}

int LuaInputValue(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnumber(state, 0.0);
        return 1;
    }
    const ScriptFunctionCallResult result = context->CallFunction("Input.Value", InputActionArgs(state));
    lua_pushnumber(state, static_cast<lua_Number>(result.Output("value").value_or(ScriptValue{ 0.0F }).AsFloat()));
    return 1;
}

int LuaInputActionFloat(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnumber(state, 0.0);
        return 1;
    }
    const ScriptFunctionCallResult result = context->CallFunction("Input.ActionFloat", InputActionArgs(state));
    lua_pushnumber(state, static_cast<lua_Number>(result.Output("value").value_or(ScriptValue{ 0.0F }).AsFloat()));
    return 1;
}

int LuaInputVector2(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    const ScriptFunctionCallResult result = context->CallFunction("Input.Vector2", InputActionArgs(state));
    lua_createtable(state, 0, 2);
    lua_pushnumber(state, static_cast<lua_Number>(result.Output("x").value_or(ScriptValue{ 0.0F }).AsFloat()));
    lua_setfield(state, -2, "x");
    lua_pushnumber(state, static_cast<lua_Number>(result.Output("y").value_or(ScriptValue{ 0.0F }).AsFloat()));
    lua_setfield(state, -2, "y");
    return 1;
}

int LuaInputAction2D(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    const ScriptFunctionCallResult result = context->CallFunction("Input.Action2D", InputActionArgs(state));
    lua_createtable(state, 0, 2);
    lua_pushnumber(state, static_cast<lua_Number>(result.Output("x").value_or(ScriptValue{ 0.0F }).AsFloat()));
    lua_setfield(state, -2, "x");
    lua_pushnumber(state, static_cast<lua_Number>(result.Output("y").value_or(ScriptValue{ 0.0F }).AsFloat()));
    lua_setfield(state, -2, "y");
    return 1;
}

int LuaInputVector3(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    const ScriptFunctionCallResult result = context->CallFunction("Input.Vector3", InputActionArgs(state));
    lua_createtable(state, 0, 3);
    lua_pushnumber(state, static_cast<lua_Number>(result.Output("x").value_or(ScriptValue{ 0.0F }).AsFloat()));
    lua_setfield(state, -2, "x");
    lua_pushnumber(state, static_cast<lua_Number>(result.Output("y").value_or(ScriptValue{ 0.0F }).AsFloat()));
    lua_setfield(state, -2, "y");
    lua_pushnumber(state, static_cast<lua_Number>(result.Output("z").value_or(ScriptValue{ 0.0F }).AsFloat()));
    lua_setfield(state, -2, "z");
    return 1;
}

[[nodiscard]] std::string LuaContextId(lua_State* state, int index) {
    if (lua_isinteger(state, index) != 0) {
        return std::to_string(static_cast<std::uint64_t>(lua_tointeger(state, index)));
    }
    const char* context = luaL_checkstring(state, index);
    return std::string{ context != nullptr ? context : "" };
}

int LuaInputAddMappingContext(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const int priority = static_cast<int>(luaL_optinteger(state, 2, 0));
    const int player = static_cast<int>(luaL_optinteger(state, 3, 0));
    std::vector<ScriptFunctionArgument> arguments{
        Arg("context", ScriptValue{ LuaContextId(state, 1) }),
        Arg("priority", ScriptValue{ priority }),
    };
    if (player > 0) {
        arguments.push_back(Arg("player", ScriptValue{ player }));
    }
    const ScriptFunctionCallResult result = context->CallFunction("Input.AddMappingContext", arguments);
    lua_pushboolean(state, result.Output("added").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaInputRemoveMappingContext(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const int player = static_cast<int>(luaL_optinteger(state, 2, 0));
    std::vector<ScriptFunctionArgument> arguments{ Arg("context", ScriptValue{ LuaContextId(state, 1) }) };
    if (player > 0) {
        arguments.push_back(Arg("player", ScriptValue{ player }));
    }
    const ScriptFunctionCallResult result = context->CallFunction("Input.RemoveMappingContext", arguments);
    lua_pushboolean(state, result.Output("removed").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaPointerPosition(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    const ScriptFunctionCallResult result = context->CallFunction("Pointer.Position", {});
    lua_createtable(state, 0, 2);
    lua_pushnumber(state, static_cast<lua_Number>(result.Output("x").value_or(ScriptValue{ 0.0F }).AsFloat()));
    lua_setfield(state, -2, "x");
    lua_pushnumber(state, static_cast<lua_Number>(result.Output("y").value_or(ScriptValue{ 0.0F }).AsFloat()));
    lua_setfield(state, -2, "y");
    return 1;
}

int LuaPointerDelta(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    const ScriptFunctionCallResult result = context->CallFunction("Pointer.Delta", {});
    lua_createtable(state, 0, 2);
    lua_pushnumber(state, static_cast<lua_Number>(result.Output("x").value_or(ScriptValue{ 0.0F }).AsFloat()));
    lua_setfield(state, -2, "x");
    lua_pushnumber(state, static_cast<lua_Number>(result.Output("y").value_or(ScriptValue{ 0.0F }).AsFloat()));
    lua_setfield(state, -2, "y");
    return 1;
}

int LuaPointerButton(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const int button = static_cast<int>(luaL_optinteger(state, 1, 0));
    const std::vector<ScriptFunctionArgument> arguments{ Arg("button", ScriptValue{ button }) };
    const ScriptFunctionCallResult result = context->CallFunction("Pointer.Button", arguments);
    lua_pushboolean(state, result.Output("pressed").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaInputRebind(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    const char* key = luaL_checkstring(state, 3);
    const int gamepadIndex = static_cast<int>(luaL_optinteger(state, 4, 0));
    const bool allowConflict = lua_toboolean(state, 5) != 0;
    const int player = static_cast<int>(luaL_optinteger(state, 6, 0));
    std::vector<ScriptFunctionArgument> arguments{
        Arg("context", ScriptValue{LuaContextId(state, 1)}),
        Arg("binding", ScriptValue{LuaContextId(state, 2)}),
        Arg("key", ScriptValue{std::string{key != nullptr ? key : ""}}),
        Arg("gamepadIndex", ScriptValue{gamepadIndex}),
        Arg("allowConflict", ScriptValue{allowConflict}),
    };
    if (player > 0) {
        arguments.push_back(Arg("player", ScriptValue{player}));
    }
    const ScriptFunctionCallResult result =
        context->CallFunction("Input.Rebind", arguments);
    lua_createtable(state, 0, 2);
    lua_pushboolean(
        state,
        result.Output("applied")
                .value_or(ScriptValue{false})
                .AsBool()
            ? 1
            : 0);
    lua_setfield(state, -2, "applied");
    const std::string conflict =
        result.Output("conflict")
            .value_or(ScriptValue{std::string{}})
            .AsString();
    lua_pushlstring(state, conflict.data(), conflict.size());
    lua_setfield(state, -2, "conflict");
    return 1;
}

int LuaInputSaveRebindProfile(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    const char* path = luaL_checkstring(state, 2);
    const int player = static_cast<int>(luaL_optinteger(state, 3, 0));
    std::vector<ScriptFunctionArgument> arguments{
        Arg("context", ScriptValue{LuaContextId(state, 1)}),
        Arg(
            "path",
            ScriptValue{std::string{path != nullptr ? path : ""}}),
    };
    if (player > 0) {
        arguments.push_back(Arg("player", ScriptValue{player}));
    }
    const ScriptFunctionCallResult result =
        context->CallFunction("Input.SaveRebindProfile", arguments);
    lua_createtable(state, 0, 2);
    lua_pushboolean(
        state,
        result.Output("saved")
                .value_or(ScriptValue{false})
                .AsBool()
            ? 1
            : 0);
    lua_setfield(state, -2, "saved");
    const std::string error =
        result.Output("error")
            .value_or(ScriptValue{std::string{}})
            .AsString();
    lua_pushlstring(state, error.data(), error.size());
    lua_setfield(state, -2, "error");
    return 1;
}

int LuaInputLoadRebindProfile(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    const char* path = luaL_checkstring(state, 2);
    const int player = static_cast<int>(luaL_optinteger(state, 3, 0));
    std::vector<ScriptFunctionArgument> arguments{
        Arg("context", ScriptValue{LuaContextId(state, 1)}),
        Arg(
            "path",
            ScriptValue{std::string{path != nullptr ? path : ""}}),
    };
    if (player > 0) {
        arguments.push_back(Arg("player", ScriptValue{player}));
    }
    const ScriptFunctionCallResult result =
        context->CallFunction("Input.LoadRebindProfile", arguments);
    lua_createtable(state, 0, 2);
    lua_pushboolean(
        state,
        result.Output("loaded")
                .value_or(ScriptValue{false})
                .AsBool()
            ? 1
            : 0);
    lua_setfield(state, -2, "loaded");
    const std::string error =
        result.Output("error")
            .value_or(ScriptValue{std::string{}})
            .AsString();
    lua_pushlstring(state, error.data(), error.size());
    lua_setfield(state, -2, "error");
    return 1;
}

int LuaPointerScroll(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnumber(state, 0.0);
        return 1;
    }
    const ScriptFunctionCallResult result = context->CallFunction("Pointer.Scroll", {});
    lua_pushnumber(state, static_cast<lua_Number>(
        result.Output("delta").value_or(ScriptValue{0.0F}).AsFloat()));
    return 1;
}

int LuaPointerRay(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    const lua_Integer player = luaL_optinteger(state, 1, 0);
    const std::array<ScriptFunctionArgument, 1> arguments{
        ScriptFunctionArgument{
            "player",
            ScriptValue{static_cast<std::int32_t>(player)}}};
    const ScriptFunctionCallResult result =
        context->CallFunction("Pointer.Ray", arguments);
    lua_createtable(state, 0, 7);
    for (const ScriptFunctionArgument& output : result.outputs) {
        PucLuaValueBridge::Push(state, output.value);
        lua_setfield(state, -2, output.name.c_str());
    }
    return 1;
}

int LuaInputPriorityConstant(lua_State* state, std::string_view functionName) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushinteger(state, 0);
        return 1;
    }
    const ScriptFunctionCallResult result = context->CallFunction(functionName, {});
    lua_pushinteger(state, static_cast<lua_Integer>(result.Output("priority").value_or(ScriptValue{ 0 }).AsInt()));
    return 1;
}

int LuaInputPriorityGameplay(lua_State* state) {
    return LuaInputPriorityConstant(state, "Input.PriorityGameplay");
}

int LuaInputPriorityUI(lua_State* state) {
    return LuaInputPriorityConstant(state, "Input.PriorityUI");
}

int LuaInputPriorityConsole(lua_State* state) {
    return LuaInputPriorityConstant(state, "Input.PriorityConsole");
}

int LuaInputPriorityDebugOverlay(lua_State* state) {
    return LuaInputPriorityConstant(state, "Input.PriorityDebugOverlay");
}

int LuaInputHasFocus(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const ScriptFunctionCallResult result = context->CallFunction("Input.HasFocus", {});
    lua_pushboolean(state, result.Output("focus").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaInputIsGamepadConnected(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const int gamepadIndex = static_cast<int>(luaL_optinteger(state, 1, 0));
    const std::vector<ScriptFunctionArgument> arguments{ Arg("gamepadIndex", ScriptValue{ gamepadIndex }) };
    const ScriptFunctionCallResult result = context->CallFunction("Input.IsGamepadConnected", arguments);
    lua_pushboolean(state, result.Output("connected").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

// LIB-153: Input.HasHaptics(gamepadIndex) -> {supported, connected, dualMotor,
// maxGamepads, reason} - the honest capability table.
int LuaInputHasHaptics(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    const int gamepadIndex = static_cast<int>(luaL_optinteger(state, 1, 0));
    const std::vector<ScriptFunctionArgument> arguments{ Arg("gamepadIndex", ScriptValue{ gamepadIndex }) };
    const ScriptFunctionCallResult result = context->CallFunction("Input.HasHaptics", arguments);
    lua_createtable(state, 0, 5);
    for (const ScriptFunctionArgument& output : result.outputs) {
        PucLuaValueBridge::Push(state, output.value);
        lua_setfield(state, -2, output.name.c_str());
    }
    return 1;
}

int LuaInputSetVibration(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("gamepadIndex", ScriptValue{ static_cast<int>(luaL_checkinteger(state, 1)) }),
        Arg("lowFrequency", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
        Arg("highFrequency", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("Input.SetVibration", arguments);
    lua_pushboolean(state, result.Output("applied").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaInputBindHapticsUser(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("localUser", ScriptValue{ static_cast<int>(luaL_checkinteger(state, 1)) }),
        Arg("gamepadIndex", ScriptValue{ static_cast<int>(luaL_checkinteger(state, 2)) }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("Input.BindHapticsUser", arguments);
    lua_pushboolean(state, result.Output("bound").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaInputSetUserVibration(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("localUser", ScriptValue{ static_cast<int>(luaL_checkinteger(state, 1)) }),
        Arg("lowFrequency", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
        Arg("highFrequency", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("Input.SetUserVibration", arguments);
    lua_pushboolean(state, result.Output("applied").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaInputStopVibration(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const ScriptFunctionCallResult result = context->CallFunction("Input.StopVibration", {});
    lua_pushboolean(state, result.Output("stopped").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaTimelineCreate(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    std::size_t length = 0U;
    const char* asset = luaL_checklstring(state, 1, &length);
    std::vector<ScriptFunctionArgument> arguments{
        Arg("asset", ScriptValue{ std::string{ asset, length } }),
    };
    if (lua_gettop(state) >= 2 && lua_isnil(state, 2) == 0) {
        arguments.push_back(Arg(
            "entity",
            ScriptValue{
                static_cast<std::uint64_t>(luaL_checkinteger(state, 2)),
                ScriptValueType::Entity }));
    }
    const ScriptFunctionCallResult result =
        context->CallFunction("Timeline.Create", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "timeline create failed");
    }
    PucLuaValueBridge::Push(
        state,
        result.Output("instance").value_or(
            ScriptValue{ 0U, ScriptValueType::Hash }));
    return 1;
}

int LuaTimelineAppliedCall(
    lua_State* state, const char* function,
    std::vector<ScriptFunctionArgument> arguments) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const ScriptFunctionCallResult result =
        context->CallFunction(function, arguments);
    lua_pushboolean(
        state,
        result.Output("applied").value_or(ScriptValue{ false }).AsBool()
            ? 1
            : 0);
    return 1;
}

int LuaTimelineRelease(lua_State* state) {
    return LuaTimelineAppliedCall(state, "Timeline.Release", {
        Arg("instance", ScriptValue{
            static_cast<std::uint64_t>(luaL_checkinteger(state, 1)),
            ScriptValueType::Hash }),
    });
}

int LuaTimelinePlay(lua_State* state) {
    return LuaTimelineAppliedCall(state, "Timeline.Play", {
        Arg("instance", ScriptValue{
            static_cast<std::uint64_t>(luaL_checkinteger(state, 1)),
            ScriptValueType::Hash }),
    });
}

int LuaTimelinePause(lua_State* state) {
    return LuaTimelineAppliedCall(state, "Timeline.Pause", {
        Arg("instance", ScriptValue{
            static_cast<std::uint64_t>(luaL_checkinteger(state, 1)),
            ScriptValueType::Hash }),
    });
}

int LuaTimelineSeek(lua_State* state) {
    return LuaTimelineAppliedCall(state, "Timeline.Seek", {
        Arg("instance", ScriptValue{
            static_cast<std::uint64_t>(luaL_checkinteger(state, 1)),
            ScriptValueType::Hash }),
        Arg("time", ScriptValue{
            static_cast<float>(luaL_checknumber(state, 2)) }),
    });
}

int LuaTimelineSkip(lua_State* state) {
    return LuaTimelineAppliedCall(state, "Timeline.Skip", {
        Arg("instance", ScriptValue{
            static_cast<std::uint64_t>(luaL_checkinteger(state, 1)),
            ScriptValueType::Hash }),
        Arg("time", ScriptValue{
            static_cast<float>(luaL_checknumber(state, 2)) }),
        Arg("emitMarkers", ScriptValue{ lua_toboolean(state, 3) != 0 }),
    });
}

int LuaTimelineBind(lua_State* state) {
    return LuaTimelineAppliedCall(state, "Timeline.Bind", {
        Arg("instance", ScriptValue{
            static_cast<std::uint64_t>(luaL_checkinteger(state, 1)),
            ScriptValueType::Hash }),
        Arg("binding", ScriptValue{ std::string{
            luaL_checkstring(state, 2) } }),
        Arg("entity", ScriptValue{
            static_cast<std::uint64_t>(luaL_checkinteger(state, 3)),
            ScriptValueType::Entity }),
    });
}

int LuaTimelineIsPlaying(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("instance", ScriptValue{
            static_cast<std::uint64_t>(luaL_checkinteger(state, 1)),
            ScriptValueType::Hash }),
    };
    const ScriptFunctionCallResult result =
        context->CallFunction("Timeline.IsPlaying", arguments);
    lua_pushboolean(
        state,
        result.Output("playing").value_or(ScriptValue{ false }).AsBool()
            ? 1
            : 0);
    return 1;
}

int LuaTimelineTime(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("instance", ScriptValue{
            static_cast<std::uint64_t>(luaL_checkinteger(state, 1)),
            ScriptValueType::Hash }),
    };
    const ScriptFunctionCallResult result =
        context->CallFunction("Timeline.Time", arguments);
    if (!result.Succeeded()) {
        return PushCallError(state, result, "timeline query failed");
    }
    lua_pushnumber(
        state,
        result.Output("time").value_or(ScriptValue{ 0.0F }).AsFloat());
    return 1;
}

int LuaUICreate(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    std::vector<ScriptFunctionArgument> arguments{
        Arg("parent", ScriptValue{ static_cast<std::uint64_t>(luaL_checkinteger(state, 1)), ScriptValueType::Hash }),
        Arg("name", ScriptValue{ std::string{ luaL_checkstring(state, 2) } }),
    };
    if (lua_gettop(state) >= 3 && lua_istable(state, 3) != 0) {
        std::vector<ScriptFunctionArgument> options = ArgumentsFromTable(state, 3);
        arguments.insert(arguments.end(), options.begin(), options.end());
    }
    const ScriptFunctionCallResult result = context->CallFunction("UI.Create", arguments);
    if (!result.Succeeded()) return PushCallError(state, result, "UI element creation failed");
    const std::optional<ScriptValue> element = result.Output("element");
    lua_pushinteger(state, static_cast<lua_Integer>(element.has_value() ? element->AsUInt64() : 0U));
    return 1;
}

int LuaUIApplied(lua_State* state, const char* function) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    std::vector<ScriptFunctionArgument> arguments{
        Arg("element", ScriptValue{ static_cast<std::uint64_t>(luaL_checkinteger(state, 1)), ScriptValueType::Hash }),
    };
    if (lua_gettop(state) >= 2 && lua_istable(state, 2) != 0) {
        std::vector<ScriptFunctionArgument> options = ArgumentsFromTable(state, 2);
        arguments.insert(arguments.end(), options.begin(), options.end());
    }
    const ScriptFunctionCallResult result = context->CallFunction(function, arguments);
    if (!result.Succeeded()) return PushCallError(state, result, "UI command was rejected");
    lua_pushboolean(state, result.Output("applied").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaUIDestroy(lua_State* state) { return LuaUIApplied(state, "UI.Destroy"); }
int LuaUIShow(lua_State* state) { return LuaUIApplied(state, "UI.Show"); }
int LuaUIHide(lua_State* state) { return LuaUIApplied(state, "UI.Hide"); }

int LuaUIFind(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    std::vector<ScriptFunctionArgument> arguments{
        Arg("name", ScriptValue{ std::string{ luaL_checkstring(state, 1) } }),
    };
    if (lua_gettop(state) >= 2 && lua_isnoneornil(state, 2) == 0) {
        const lua_Integer entity = luaL_checkinteger(state, 2);
        if (entity < 0) {
            lua_pushnil(state);
            return 1;
        }
        arguments.push_back(Arg("entity", ScriptValue{ static_cast<std::uint64_t>(entity), ScriptValueType::Entity }));
    }
    const ScriptFunctionCallResult result = context->CallFunction("UI.Find", arguments);
    if (!result.Succeeded()) return PushCallError(state, result, "UI setup lookup failed");
    if (!result.Output("found").value_or(ScriptValue{ false }).AsBool()) {
        lua_pushnil(state);
        return 1;
    }
    PucLuaValueBridge::Push(state, result.Output("element").value_or(ScriptValue{ 0U, ScriptValueType::Hash }));
    return 1;
}

enum class LuaUIValueKind : std::uint8_t { String, Hash, Bool, Float };

int LuaUISetValue(lua_State* state, const char* function, const char* field, LuaUIValueKind kind) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    std::vector<ScriptFunctionArgument> arguments{
        Arg("element", ScriptValue{ static_cast<std::uint64_t>(luaL_checkinteger(state, 1)), ScriptValueType::Hash }),
    };
    switch (kind) {
    case LuaUIValueKind::String:
        arguments.push_back(Arg(field, ScriptValue{ std::string{ luaL_checkstring(state, 2) } }));
        break;
    case LuaUIValueKind::Hash:
        arguments.push_back(Arg(field, ScriptValue{ static_cast<std::uint64_t>(luaL_checkinteger(state, 2)), ScriptValueType::Hash }));
        break;
    case LuaUIValueKind::Bool:
        arguments.push_back(Arg(field, ScriptValue{ lua_toboolean(state, 2) != 0 }));
        break;
    case LuaUIValueKind::Float:
        arguments.push_back(Arg(field, ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }));
        break;
    }
    if (lua_gettop(state) >= 3 && lua_istable(state, 3) != 0) {
        std::vector<ScriptFunctionArgument> options = ArgumentsFromTable(state, 3);
        arguments.insert(arguments.end(), options.begin(), options.end());
    }
    const ScriptFunctionCallResult result = context->CallFunction(function, arguments);
    if (!result.Succeeded()) return PushCallError(state, result, "UI control command was rejected");
    lua_pushboolean(state, result.Output("applied").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaUISetText(lua_State* state) { return LuaUISetValue(state, "UI.SetText", "text", LuaUIValueKind::String); }
int LuaUISetImage(lua_State* state) { return LuaUISetValue(state, "UI.SetImage", "image", LuaUIValueKind::Hash); }
int LuaUISetToggle(lua_State* state) { return LuaUISetValue(state, "UI.SetToggle", "value", LuaUIValueKind::Bool); }
int LuaUISetSlider(lua_State* state) { return LuaUISetValue(state, "UI.SetSlider", "value", LuaUIValueKind::Float); }
int LuaUIListAppend(lua_State* state) { return LuaUISetValue(state, "UI.ListAppend", "item", LuaUIValueKind::String); }
int LuaUIListClear(lua_State* state) { return LuaUIApplied(state, "UI.ListClear"); }
int LuaUIConfigureList(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    const lua_Integer viewport = luaL_checkinteger(state, 2);
    const lua_Integer overscan = luaL_checkinteger(state, 3);
    if (viewport < 0 || overscan < 0 || viewport > static_cast<lua_Integer>(std::numeric_limits<std::uint32_t>::max()) ||
        overscan > static_cast<lua_Integer>(std::numeric_limits<std::uint32_t>::max())) {
        return luaL_error(state, "UI.ConfigureList viewport and overscan must be unsigned integers");
    }
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("element", ScriptValue{ static_cast<std::uint64_t>(luaL_checkinteger(state, 1)), ScriptValueType::Hash }),
        Arg("viewportItems", ScriptValue{ static_cast<std::uint32_t>(viewport) }),
        Arg("overscan", ScriptValue{ static_cast<std::uint32_t>(overscan) }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("UI.ConfigureList", arguments);
    if (!result.Succeeded()) return PushCallError(state, result, "UI virtual list configuration was rejected");
    lua_pushboolean(state, result.Output("applied").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}
int LuaUIListScrollTo(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    const lua_Integer index = luaL_checkinteger(state, 2);
    if (index < 0 || index > static_cast<lua_Integer>(std::numeric_limits<std::uint32_t>::max())) {
        return luaL_error(state, "UI.ListScrollTo index must be an unsigned integer");
    }
    const std::vector<ScriptFunctionArgument> arguments{
        Arg("element", ScriptValue{ static_cast<std::uint64_t>(luaL_checkinteger(state, 1)), ScriptValueType::Hash }),
        Arg("firstVisibleIndex", ScriptValue{ static_cast<std::uint32_t>(index) }),
    };
    const ScriptFunctionCallResult result = context->CallFunction("UI.ListScrollTo", arguments);
    if (!result.Succeeded()) return PushCallError(state, result, "UI virtual list scroll was rejected");
    lua_pushboolean(state, result.Output("applied").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}
int LuaUISetScrollOffset(lua_State* state) { return LuaUISetValue(state, "UI.SetScrollOffset", "offset", LuaUIValueKind::Float); }
int LuaUISetModalOpen(lua_State* state) { return LuaUISetValue(state, "UI.SetModalOpen", "open", LuaUIValueKind::Bool); }

int LuaUIEmitPosition(lua_State* state, const char* function) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        lua_pushnil(state);
        lua_pushliteral(state, "lua script execution context is not available");
        return 2;
    }
    std::vector<ScriptFunctionArgument> arguments{
        Arg("element", ScriptValue{ static_cast<std::uint64_t>(luaL_checkinteger(state, 1)), ScriptValueType::Hash }),
        Arg("x", ScriptValue{ static_cast<float>(luaL_checknumber(state, 2)) }),
        Arg("y", ScriptValue{ static_cast<float>(luaL_checknumber(state, 3)) }),
    };
    if (lua_gettop(state) >= 4 && lua_istable(state, 4) != 0) {
        std::vector<ScriptFunctionArgument> options = ArgumentsFromTable(state, 4);
        arguments.insert(arguments.end(), options.begin(), options.end());
    }
    const ScriptFunctionCallResult result = context->CallFunction(function, arguments);
    if (!result.Succeeded()) return PushCallError(state, result, "UI event was rejected");
    lua_pushboolean(state, result.Output("applied").value_or(ScriptValue{ false }).AsBool() ? 1 : 0);
    return 1;
}

int LuaUIEmitClick(lua_State* state) { return LuaUIEmitPosition(state, "UI.EmitClick"); }
int LuaUIEmitPointer(lua_State* state) { return LuaUIEmitPosition(state, "UI.EmitPointer"); }
int LuaUIEmitSubmit(lua_State* state) { return LuaUISetValue(state, "UI.EmitSubmit", "text", LuaUIValueKind::String); }
int LuaUIEmitChanged(lua_State* state) { return LuaUISetValue(state, "UI.EmitChanged", "value", LuaUIValueKind::Float); }
int LuaUIEmitFocus(lua_State* state) { return LuaUISetValue(state, "UI.EmitFocus", "focused", LuaUIValueKind::Bool); }
int LuaUIEmitNavigation(lua_State* state) { return LuaUISetValue(state, "UI.EmitNavigation", "direction", LuaUIValueKind::String); }

// LIB-010: SetClosure's `function` is a runtime value (123 call sites each
// pass a different one), so it cannot become PucLuaSafeCall's compile-time
// template parameter without touching every call site. Instead this
// trampoline carries `function` as a second upvalue (appended AFTER the
// existing context upvalue, so every LuaXxx body's own
// lua_upvalueindex(1)==context read is completely unaffected — Lua's
// "currently running closure" while function(state) executes here is still
// this trampoline's own frame, holding both upvalues unchanged) and routes
// through the same PucLuaSafeInvoke every other registration site uses.
int SetClosureTrampoline(lua_State* state) {
    const auto function = reinterpret_cast<lua_CFunction>(lua_touserdata(state, lua_upvalueindex(2)));
    return PucLuaSafeInvoke(state, function);
}

void SetClosure(lua_State* state, const char* name, lua_CFunction function, ScriptExecutionContext& context) {
    lua_pushlightuserdata(state, &context);
    lua_pushlightuserdata(state, reinterpret_cast<void*>(function));
    lua_pushcclosure(state, &SetClosureTrampoline, 2);
    lua_setfield(state, -2, name);
}

} // namespace

void PucLuaFunctionApi::Attach(lua_State* state, int environmentIndex, ScriptExecutionContext& context) {
    lua_pushlightuserdata(state, &context);
    lua_pushcclosure(state, &PucLuaSafeCall<&LuaCallFunction>, 1);
    lua_setfield(state, environmentIndex, "CallFunction");

    lua_pushlightuserdata(state, &context);
    lua_pushcclosure(state, &PucLuaSafeCall<&LuaLog>, 1);
    lua_setfield(state, environmentIndex, "Log");

    lua_createtable(state, 0, 23);
    lua_pushlightuserdata(state, &context);
    lua_pushcclosure(state, &PucLuaSafeCall<&LuaAudioPlay>, 1);
    lua_setfield(state, -2, "Play");
    SetClosure(state, "SetMixer", &LuaAudioSetMixer, context);
    SetClosure(state, "ActiveMixer", &LuaAudioActiveMixer, context);
    SetClosure(state, "SetSnapshot", &LuaAudioSetSnapshot, context);
    SetClosure(state, "ActiveSnapshot", &LuaAudioActiveSnapshot, context);
    SetClosure(state, "Stop", &LuaAudioStop, context);
    SetClosure(state, "Pause", &LuaAudioPause, context);
    SetClosure(state, "Resume", &LuaAudioResume, context);
    SetClosure(state, "Seek", &LuaAudioSeek, context);
    SetClosure(state, "SetVolume", &LuaAudioSetVolume, context);
    SetClosure(state, "SetPitch", &LuaAudioSetPitch, context);
    SetClosure(state, "SetLoop", &LuaAudioSetLoop, context);
    SetClosure(state, "IsPlaying", &LuaAudioIsPlaying, context);
    SetClosure(state, "SetBusVolume", &LuaAudioSetBusVolume, context);
    SetClosure(state, "ClearBusVolume", &LuaAudioClearBusVolume, context);
    SetClosure(state, "TransitionToSnapshot", &LuaAudioTransitionToSnapshot, context);
    SetClosure(state, "ConfigureOcclusion", &LuaAudioConfigureOcclusion, context);
    SetClosure(state, "OcclusionEnabled", &LuaAudioOcclusionEnabled, context);
    SetClosure(state, "GetPosition", &LuaAudioGetPosition, context);
    SetClosure(state, "AddMarker", &LuaAudioAddMarker, context);
    lua_setfield(state, environmentIndex, "Audio");

    lua_createtable(state, 0, 7);
    SetClosure(state, "SetMesh", &LuaMeshRendererSetMesh, context);
    SetClosure(state, "SetMaterial", &LuaMeshRendererSetMaterial, context);
    SetClosure(state, "SetMaterialSlot", &LuaMeshRendererSetMaterialSlot, context);
    SetClosure(state, "GetMaterialSlot", &LuaMeshRendererGetMaterialSlot, context);
    SetClosure(state, "ClearMaterialSlot", &LuaMeshRendererClearMaterialSlot, context);
    SetClosure(state, "SetMaterialInstance", &LuaMeshRendererSetMaterialInstance, context);
    SetClosure(state, "ClearMaterialInstance", &LuaMeshRendererClearMaterialInstance, context);
    lua_setfield(state, environmentIndex, "MeshRenderer");

    lua_createtable(state, 0, 7);
    SetClosure(state, "Create", &LuaMaterialInstanceCreate, context);
    SetClosure(state, "Release", &LuaMaterialInstanceRelease, context);
    SetClosure(state, "Exists", &LuaMaterialInstanceExists, context);
    SetClosure(state, "Parent", &LuaMaterialInstanceParent, context);
    SetClosure(state, "SetParameterScalar", &LuaMaterialInstanceSetParameterScalar, context);
    SetClosure(state, "SetParameterBool", &LuaMaterialInstanceSetParameterBool, context);
    SetClosure(state, "ClearParameter", &LuaMaterialInstanceClearParameter, context);
    lua_setfield(state, environmentIndex, "MaterialInstance");

    lua_createtable(state, 0, 3);
    SetClosure(state, "SetProfile", &LuaPostProcessSetProfile, context);
    SetClosure(state, "ClearProfile", &LuaPostProcessClearProfile, context);
    SetClosure(state, "ActiveProfile", &LuaPostProcessActiveProfile, context);
    lua_setfield(state, environmentIndex, "PostProcess");

    lua_createtable(state, 0, 11);
    SetClosure(state, "Create", &LuaParticlesCreate, context);
    SetClosure(state, "Release", &LuaParticlesRelease, context);
    SetClosure(state, "Exists", &LuaParticlesExists, context);
    SetClosure(state, "Play", &LuaParticlesPlay, context);
    SetClosure(state, "Stop", &LuaParticlesStop, context);
    SetClosure(state, "IsPlaying", &LuaParticlesIsPlaying, context);
    SetClosure(state, "SetSeed", &LuaParticlesSetSeed, context);
    SetClosure(state, "SetParameterScalar", &LuaParticlesSetParameterScalar, context);
    SetClosure(state, "ClearParameter", &LuaParticlesClearParameter, context);
    SetClosure(state, "Emit", &LuaParticlesEmit, context);
    SetClosure(state, "LiveCount", &LuaParticlesLiveCount, context);
    lua_setfield(state, environmentIndex, "Particles");

    lua_createtable(state, 0, 9);
    SetClosure(state, "IsVisible", &LuaRendererIsVisible, context);
    SetClosure(state, "GetBounds", &LuaRendererGetBounds, context);
    SetClosure(state, "TestFrustum", &LuaRendererTestFrustum, context);
    SetClosure(state, "HasFrame", &LuaRendererHasFrame, context);
    SetClosure(state, "WorldToScreen", &LuaRendererWorldToScreen, context);
    SetClosure(state, "ScreenPointToRay", &LuaRendererScreenPointToRay, context);
    SetClosure(state, "ScreenToWorld", &LuaRendererScreenToWorld, context);
    SetClosure(state, "CaptureScreen", &LuaRendererCaptureScreen, context);
    SetClosure(state, "CaptureStatus", &LuaRendererCaptureStatus, context);
    lua_setfield(state, environmentIndex, "Renderer");

    lua_createtable(state, 0, 10);
    SetClosure(state, "FindByName", &LuaWorldFindByName, context);
    SetClosure(state, "FindByTag", &LuaWorldFindByTag, context);
    SetClosure(state, "Exists", &LuaWorldExists, context);
    SetClosure(state, "Name", &LuaWorldName, context);
    SetClosure(state, "Spawn", &LuaWorldSpawn, context);
    SetClosure(state, "Destroy", &LuaWorldDestroy, context);
    SetClosure(state, "SetTag", &LuaWorldSetTag, context);
    SetClosure(state, "HasTag", &LuaWorldHasTag, context);
    SetClosure(state, "SetParent", &LuaWorldSetParent, context);
    SetClosure(state, "InstantiatePrefab", &LuaWorldInstantiatePrefab, context);
    lua_setfield(state, environmentIndex, "World");

    lua_createtable(state, 0, 6);
    SetClosure(state, "Load", &LuaSceneLoad, context);
    SetClosure(state, "Unload", &LuaSceneUnload, context);
    SetClosure(state, "SetActive", &LuaSceneSetActive, context);
    SetClosure(state, "GetActive", &LuaSceneGetActive, context);
    SetClosure(state, "Find", &LuaSceneFind, context);
    SetClosure(state, "LoadProgress", &LuaSceneLoadProgress, context);
    lua_setfield(state, environmentIndex, "Scene");

    lua_createtable(state, 0, 1);
    SetClosure(state, "delta", &LuaTimeDelta, context);
    lua_setfield(state, environmentIndex, "Time");

    lua_createtable(state, 0, 3);
    SetClosure(state, "GetPosition", &LuaTransformGetPosition, context);
    SetClosure(state, "SetPosition", &LuaTransformSetPosition, context);
    SetClosure(state, "Translate", &LuaTransformTranslate, context);
    lua_setfield(state, environmentIndex, "Transform");

    lua_createtable(state, 0, 27);
    SetClosure(state, "Raycast", &LuaPhysicsRaycast, context);
    SetClosure(state, "AddForce", &LuaPhysicsAddForce, context);
    SetClosure(state, "AddImpulse", &LuaPhysicsAddImpulse, context);
    SetClosure(state, "SetVelocity", &LuaPhysicsSetVelocity, context);
    SetClosure(state, "GetVelocity", &LuaPhysicsGetVelocity, context);
    SetClosure(state, "SetAngularVelocity", &LuaPhysicsSetAngularVelocity, context);
    SetClosure(state, "GetAngularVelocity", &LuaPhysicsGetAngularVelocity, context);
    SetClosure(state, "MoveKinematic", &LuaPhysicsMoveKinematic, context);
    SetClosure(state, "Sleep", &LuaPhysicsSleep, context);
    SetClosure(state, "Wake", &LuaPhysicsWake, context);
    SetClosure(state, "IsSleeping", &LuaPhysicsIsSleeping, context);
    SetClosure(state, "SphereCast", &LuaPhysicsSphereCast, context);
    SetClosure(state, "BoxCast", &LuaPhysicsBoxCast, context);
    SetClosure(state, "CapsuleCast", &LuaPhysicsCapsuleCast, context);
    SetClosure(state, "OverlapSphere", &LuaPhysicsOverlapSphere, context);
    SetClosure(state, "OverlapBox", &LuaPhysicsOverlapBox, context);
    SetClosure(state, "OverlapCapsule", &LuaPhysicsOverlapCapsule, context);
    SetClosure(state, "ClosestPoint", &LuaPhysicsClosestPoint, context);
    SetClosure(state, "LayerBit", &LuaPhysicsLayerBit, context);
    SetClosure(state, "CharacterMove", &LuaPhysicsCharacterMove, context);
    SetClosure(state, "CharacterJump", &LuaPhysicsCharacterJump, context);
    SetClosure(state, "CharacterVelocity", &LuaPhysicsCharacterVelocity, context);
    SetClosure(state, "CharacterIsGrounded", &LuaPhysicsCharacterIsGrounded, context);
    SetClosure(state, "CharacterGroundNormal", &LuaPhysicsCharacterGroundNormal, context);
    SetClosure(state, "CharacterGroundVelocity", &LuaPhysicsCharacterGroundVelocity, context);
    SetClosure(state, "SetDebugDrawEnabled", &LuaPhysicsSetDebugDrawEnabled, context);
    SetClosure(state, "IsDebugDrawEnabled", &LuaPhysicsIsDebugDrawEnabled, context);
    lua_setfield(state, environmentIndex, "Physics");

    lua_createtable(state, 0, 20);
    SetClosure(state, "IsPressed", &LuaInputIsPressed, context);
    SetClosure(state, "WasPressed", &LuaInputWasPressed, context);
    SetClosure(state, "WasReleased", &LuaInputWasReleased, context);
    SetClosure(state, "Value", &LuaInputValue, context);
    SetClosure(state, "Vector2", &LuaInputVector2, context);
    SetClosure(state, "Vector3", &LuaInputVector3, context);
    SetClosure(state, "AddMappingContext", &LuaInputAddMappingContext, context);
    SetClosure(state, "RemoveMappingContext", &LuaInputRemoveMappingContext, context);
    SetClosure(state, "Rebind", &LuaInputRebind, context);
    SetClosure(
        state, "SaveRebindProfile", &LuaInputSaveRebindProfile, context);
    SetClosure(
        state, "LoadRebindProfile", &LuaInputLoadRebindProfile, context);
    SetClosure(state, "ActionBool", &LuaInputActionBool, context);
    SetClosure(state, "ActionFloat", &LuaInputActionFloat, context);
    SetClosure(state, "Action2D", &LuaInputAction2D, context);
    SetClosure(state, "Pressed", &LuaInputPressed, context);
    SetClosure(state, "Released", &LuaInputReleased, context);
    SetClosure(state, "Held", &LuaInputHeld, context);
    SetClosure(state, "PriorityGameplay", &LuaInputPriorityGameplay, context);
    SetClosure(state, "PriorityUI", &LuaInputPriorityUI, context);
    SetClosure(state, "PriorityConsole", &LuaInputPriorityConsole, context);
    SetClosure(state, "PriorityDebugOverlay", &LuaInputPriorityDebugOverlay, context);
    SetClosure(state, "HasFocus", &LuaInputHasFocus, context);
    SetClosure(state, "IsGamepadConnected", &LuaInputIsGamepadConnected, context);
    SetClosure(state, "HasHaptics", &LuaInputHasHaptics, context);
    SetClosure(state, "SetVibration", &LuaInputSetVibration, context);
    SetClosure(state, "BindHapticsUser", &LuaInputBindHapticsUser, context);
    SetClosure(state, "SetUserVibration", &LuaInputSetUserVibration, context);
    SetClosure(state, "StopVibration", &LuaInputStopVibration, context);
    lua_setfield(state, environmentIndex, "Input");

    lua_createtable(state, 0, 9);
    SetClosure(state, "Create", &LuaTimelineCreate, context);
    SetClosure(state, "Release", &LuaTimelineRelease, context);
    SetClosure(state, "Play", &LuaTimelinePlay, context);
    SetClosure(state, "Pause", &LuaTimelinePause, context);
    SetClosure(state, "Seek", &LuaTimelineSeek, context);
    SetClosure(state, "Skip", &LuaTimelineSkip, context);
    SetClosure(state, "Bind", &LuaTimelineBind, context);
    SetClosure(state, "IsPlaying", &LuaTimelineIsPlaying, context);
    SetClosure(state, "Time", &LuaTimelineTime, context);
    lua_setfield(state, environmentIndex, "Timeline");

    lua_createtable(state, 0, 21);
    SetClosure(state, "Create", &LuaUICreate, context);
    SetClosure(state, "Destroy", &LuaUIDestroy, context);
    SetClosure(state, "Show", &LuaUIShow, context);
    SetClosure(state, "Hide", &LuaUIHide, context);
    SetClosure(state, "Find", &LuaUIFind, context);
    SetClosure(state, "SetText", &LuaUISetText, context);
    SetClosure(state, "SetImage", &LuaUISetImage, context);
    SetClosure(state, "SetToggle", &LuaUISetToggle, context);
    SetClosure(state, "SetSlider", &LuaUISetSlider, context);
    SetClosure(state, "ListAppend", &LuaUIListAppend, context);
    SetClosure(state, "ListClear", &LuaUIListClear, context);
    SetClosure(state, "ConfigureList", &LuaUIConfigureList, context);
    SetClosure(state, "ListScrollTo", &LuaUIListScrollTo, context);
    SetClosure(state, "SetScrollOffset", &LuaUISetScrollOffset, context);
    SetClosure(state, "SetModalOpen", &LuaUISetModalOpen, context);
    SetClosure(state, "EmitClick", &LuaUIEmitClick, context);
    SetClosure(state, "EmitPointer", &LuaUIEmitPointer, context);
    SetClosure(state, "EmitSubmit", &LuaUIEmitSubmit, context);
    SetClosure(state, "EmitChanged", &LuaUIEmitChanged, context);
    SetClosure(state, "EmitFocus", &LuaUIEmitFocus, context);
    SetClosure(state, "EmitNavigation", &LuaUIEmitNavigation, context);
    lua_setfield(state, environmentIndex, "UI");

    lua_createtable(state, 0, 5);
    SetClosure(state, "Position", &LuaPointerPosition, context);
    SetClosure(state, "Delta", &LuaPointerDelta, context);
    SetClosure(state, "Button", &LuaPointerButton, context);
    SetClosure(state, "Scroll", &LuaPointerScroll, context);
    SetClosure(state, "Ray", &LuaPointerRay, context);
    lua_setfield(state, environmentIndex, "Pointer");
}

} // namespace kb::script
