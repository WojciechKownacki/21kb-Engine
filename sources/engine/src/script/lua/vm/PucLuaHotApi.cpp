// The hot engine calls as plain C functions for LuaJIT's FFI (see PucLuaHotApi.hpp). They make the same engine calls as
// the C API bindings in script/lua/api/PucLuaFunctionApi.cpp, so a hot call and an ordinary one do the same thing.
#include "script/lua/LuaVm.hpp"
#include "script/lua/PucLuaHotApi.hpp"

#include "script/lua/PucLuaRuntimeApi.hpp"
#include "script/lua/PucLuaStateUtilities.hpp"

#include "engine/input/InputSubsystem.hpp"
#include "engine/script/ScriptExecutionContext.hpp"
#include "engine/script/ScriptInputApi.hpp"
#include "engine/script/ScriptTimeApi.hpp"
#include "engine/script/ScriptTransformApi.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace kb::script {
namespace {

// Every function is handed the slot of the runtime that runs the entry (the one the API closures read their context from).
[[nodiscard]] ScriptExecutionContext* ActiveContext(void* slot) noexcept {
    return slot != nullptr ? static_cast<PucLuaStateLifetime*>(slot)->activeContext : nullptr;
}

// Arguments are checked on the Lua side: the entity is an integral number below 2^53 and the player a small integral
// one; anything else takes the C API function, which reports the error.
int HotGetPosition(void* slot, double entity, float* out) noexcept {
    ScriptExecutionContext* context = ActiveContext(slot);
    if (context == nullptr) return 0;
    try {
        return ScriptTransformApi::GetLocalPosition(context->GetScene(), kb::scene::SceneEntity{ static_cast<std::uint64_t>(entity) }, out[0], out[1], out[2]) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int HotSetPosition(void* slot, double entity, double x, double y, double z) noexcept {
    ScriptExecutionContext* context = ActiveContext(slot);
    if (context == nullptr) return 0;
    try {
        return ScriptTransformApi::SetLocalPosition(context->GetScene(), kb::scene::SceneEntity{ static_cast<std::uint64_t>(entity) },
            static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int HotIsPressed(void* slot, const char* action, int player) noexcept {
    ScriptExecutionContext* context = ActiveContext(slot);
    if (context == nullptr) return 0;
    try {
        return context->GetScene().Input(ScriptInputApi::UserForPlayer(player)).IsActionPressed(std::string_view{ action }) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int HotVector2(void* slot, const char* action, int player, double* out) noexcept {
    ScriptExecutionContext* context = ActiveContext(slot);
    out[0] = 0.0;
    out[1] = 0.0;
    if (context == nullptr) return 0;
    try {
        const kb::input::InputValue value = ScriptInputApi::ActionValue(context->GetScene(), player, std::string_view{ action });
        out[0] = static_cast<double>(value.x);
        out[1] = static_cast<double>(value.y);
        return 1;
    } catch (...) {
        return 0;
    }
}

double HotValue(void* slot, const char* action, int player) noexcept {
    ScriptExecutionContext* context = ActiveContext(slot);
    if (context == nullptr) return 0.0;
    try {
        return static_cast<double>(ScriptInputApi::ActionValue(context->GetScene(), player, std::string_view{ action }).AsAxis1D());
    } catch (...) {
        return 0.0;
    }
}

double HotDelta(void* slot) noexcept {
    ScriptExecutionContext* context = ActiveContext(slot);
    if (context == nullptr) return 0.0;
    try {
        return static_cast<double>(ScriptTimeApi::ScaledDelta(&context->GetScene(), context->DeltaSeconds()));
    } catch (...) {
        return 0.0;
    }
}

struct HotApi {
    int (*getPosition)(void*, double, float*) noexcept;
    int (*setPosition)(void*, double, double, double, double) noexcept;
    int (*isPressed)(void*, const char*, int) noexcept;
    int (*vector2)(void*, const char*, int, double*) noexcept;
    double (*value)(void*, const char*, int) noexcept;
    double (*delta)(void*) noexcept;
};

const HotApi hotApi{ &HotGetPosition, &HotSetPosition, &HotIsPressed, &HotVector2, &HotValue, &HotDelta };

// The Lua side: trusted code that only the runtime runs. It gets the ffi library, the table above and the slot, and returns
// the function that replaces the hot functions of one script's API tables. The functions keep the results of the C API ones
// (nil for a position that cannot be read, a boolean for a write); a call whose arguments are not the plain numeric and
// string forms is a tail call of the original, so its error is the C API's, reported at the script's line.
constexpr const char* kHotApiPrelude = R"(
local ffi, apiPointer, slotPointer = ...
ffi.cdef[[
typedef struct {
  int (*get_position)(void*, double, float*);
  int (*set_position)(void*, double, double, double, double);
  int (*is_pressed)(void*, const char*, int);
  int (*vector2)(void*, const char*, int, double*);
  double (*value)(void*, const char*, int);
  double (*delta)(void*);
} KbHotApi;
]]
local api = ffi.cast("const KbHotApi*", apiPointer)
local slot = ffi.cast("void*", slotPointer)
local get_position, set_position, is_pressed = api.get_position, api.set_position, api.is_pressed
local vector2, value, delta = api.vector2, api.value, api.delta
local position = ffi.new("float[3]")
local pair = ffi.new("double[2]")
local type, floor = type, math.floor
local LIMIT = 9007199254740992
local PLAYERS = 2147483647
return function(Transform, Input, Time)
  if Transform then
    local original_position, original_set, original_table = Transform.GetPositionXYZ, Transform.SetPosition, Transform.GetPosition
    Transform.GetPosition = function(entity)
      if type(entity) == "number" and entity >= 0 and entity < LIMIT and floor(entity) == entity then
        if get_position(slot, entity, position) ~= 0 then return { x = position[0], y = position[1], z = position[2] } end
        return nil
      end
      return original_table(entity)
    end
    Transform.GetPositionXYZ = function(entity)
      if type(entity) == "number" and entity >= 0 and entity < LIMIT and floor(entity) == entity then
        if get_position(slot, entity, position) ~= 0 then return position[0], position[1], position[2] end
        return nil
      end
      return original_position(entity)
    end
    Transform.SetPosition = function(entity, x, y, z)
      if type(entity) == "number" and type(x) == "number" and type(y) == "number" and type(z) == "number"
          and entity >= 0 and entity < LIMIT and floor(entity) == entity then
        return set_position(slot, entity, x, y, z) ~= 0
      end
      return original_set(entity, x, y, z)
    end
  end
  if Input then
    local original_pressed, original_value, original_vector, original_table = Input.IsPressed, Input.Value, Input.Vector2XY, Input.Vector2
    Input.Vector2 = function(action, player)
      if type(action) == "string" and (player == nil or (type(player) == "number" and player >= 0 and player < PLAYERS and floor(player) == player)) then
        vector2(slot, action, player or 0, pair)
        return { x = pair[0], y = pair[1] }
      end
      return original_table(action, player)
    end
    Input.IsPressed = function(action, player)
      if type(action) == "string" and (player == nil or (type(player) == "number" and player >= 0 and player < PLAYERS and floor(player) == player)) then
        return is_pressed(slot, action, player or 0) ~= 0
      end
      return original_pressed(action, player)
    end
    Input.Value = function(action, player)
      if type(action) == "string" and (player == nil or (type(player) == "number" and player >= 0 and player < PLAYERS and floor(player) == player)) then
        return value(slot, action, player or 0)
      end
      return original_value(action, player)
    end
    Input.Vector2XY = function(action, player)
      if type(action) == "string" and (player == nil or (type(player) == "number" and player >= 0 and player < PLAYERS and floor(player) == player)) then
        vector2(slot, action, player or 0, pair)
        return pair[0], pair[1]
      end
      return original_vector(action, player)
    end
  end
  if Time then
    Time.delta = function() return delta(slot) end
    Time.Delta = Time.delta
  end
end
)";

// Registry key of the function the prelude returns (false when it could not be built).
constexpr char kInstallerKey = 0;

// Builds the installer once per state: the ffi library is opened for the runtime only and removed from the globals again,
// so no script reaches it. Leaves the installer on the stack.
bool PushInstaller(lua_State* state) {
    if (lua_rawgetp(state, LUA_REGISTRYINDEX, &kInstallerKey) == LUA_TFUNCTION) return true;
    const bool failedBefore = lua_type(state, -1) == LUA_TBOOLEAN;
    lua_pop(state, 1);
    if (failedBefore) return false;
    const int top = lua_gettop(state);
    bool built = false;
    lua_pushcfunction(state, luaopen_ffi);
    lua_pushstring(state, LUA_FFILIBNAME);
    lua_call(state, 1, 1);
    lua_pushnil(state);
    lua_setglobal(state, LUA_FFILIBNAME);
    if (luaL_loadbuffer(state, kHotApiPrelude, std::char_traits<char>::length(kHotApiPrelude), "=hot-api") == 0) {
        lua_pushvalue(state, -2);
        lua_pushlightuserdata(state, const_cast<HotApi*>(&hotApi));
        lua_pushlightuserdata(state, PucLuaRuntimeApi::ContextSlot(state));
        built = lua_pcall(state, 3, 1, 0) == 0 && lua_isfunction(state, -1) != 0;
    }
    if (built) {
        lua_pushvalue(state, -1);
    } else {
        lua_pushboolean(state, 0);
    }
    lua_rawsetp(state, LUA_REGISTRYINDEX, &kInstallerKey);
    if (!built) {
        lua_settop(state, top);
        return false;
    }
    lua_replace(state, top + 1);
    lua_settop(state, top + 1);
    return true;
}

} // namespace

void PucLuaHotApi::Attach(lua_State* state, int environmentIndex) {
    environmentIndex = lua_absindex(state, environmentIndex);
    if (!PushInstaller(state)) return;
    for (const char* name : { "Transform", "Input", "Time" }) {
        lua_getfield(state, environmentIndex, name);
        if (lua_istable(state, -1) == 0) {
            lua_pop(state, 1);
            lua_pushnil(state);
        }
    }
    if (lua_pcall(state, 3, 0, 0) != 0) lua_pop(state, 1);
}

} // namespace kb::script
