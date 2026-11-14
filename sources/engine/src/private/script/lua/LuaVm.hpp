#pragma once

// The Lua C API as the engine's script sources see it. The build selects one script VM (CMake KB_SCRIPT_VM): LuaJIT
// where the platform allows generated machine code (Windows, Linux, Android), Lua 5.4 elsewhere. Both have the same
// stack-based API; this header is the one place that maps the few calls that differ, so the binding and runtime
// sources are written once. KB_SCRIPT_VM_LUAJIT is defined for the sources that include it when LuaJIT is selected.
// It must be the first include of every source file that touches the Lua API.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#if defined(KB_SCRIPT_VM_LUAJIT)
#include <luajit.h>
#endif
}

namespace kb::script {

// Every runtime state keeps pointers to what belongs to it. The state's allocator user data begins with this slot, so
// they are found from any thread of the state with one call, on both VMs (neither Lua 5.4's per-thread extra space nor
// a per-state user pointer, which LuaJIT lacks, is needed).
struct PucLuaOwnerSlot {
    void* owner = nullptr;   // the PucLuaScriptRuntime
    void* runaway = nullptr; // its execution budget
};

[[nodiscard]] inline PucLuaOwnerSlot* LuaStateSlot(lua_State* state) noexcept {
    void* userData = nullptr;
    static_cast<void>(lua_getallocf(state, &userData));
    return static_cast<PucLuaOwnerSlot*>(userData);
}

[[nodiscard]] inline void* LuaStateOwner(lua_State* state) noexcept {
    const PucLuaOwnerSlot* slot = LuaStateSlot(state);
    return slot != nullptr ? slot->owner : nullptr;
}

} // namespace kb::script

#if defined(KB_SCRIPT_VM_LUAJIT)

// LuaJIT: the C API of Lua 5.1 with a few 5.2 additions. Neither VM has the pieces below in the other's form: the
// functions named here give the Lua 5.4 behaviour the engine sources are written for.
using lua_Unsigned = unsigned long long;

// Ids are exact up to 2^53 (a number is an integer when it has no fraction), as in the Lua 5.4 runtime.
inline bool kb_lua_is_integral(double value) noexcept {
    return value == std::floor(value) && std::fabs(value) < 9007199254740992.0;
}

inline lua_Integer kb_lua_tointegerx(lua_State* state, int index, int* isInteger) {
    int isNumber = 0;
    const double number = lua_tonumberx(state, index, &isNumber);
    const bool integral = isNumber != 0 && kb_lua_is_integral(number);
    if (isInteger != nullptr) *isInteger = integral ? 1 : 0;
    return integral ? static_cast<lua_Integer>(number) : 0;
}

inline int kb_lua_isinteger(lua_State* state, int index) {
    return lua_type(state, index) == LUA_TNUMBER && kb_lua_is_integral(lua_tonumber(state, index)) ? 1 : 0;
}

// lua_error never returns; the binding code writes `return lua_error(state)` as Lua 5.4 declares it.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4702)
#endif
inline int kb_lua_error(lua_State* state) {
    lua_error(state);
    return 0;
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#undef lua_tointegerx
#undef lua_error
#define lua_tointegerx(state, index, isInteger) kb_lua_tointegerx(state, index, isInteger)
#define lua_isinteger(state, index) kb_lua_isinteger(state, index)
#define lua_error(state) kb_lua_error(state)
#define lua_rawlen(state, index) lua_objlen(state, index)
// The index of a table slot is an int in LuaJIT and the integer type in Lua 5.4.
#define lua_rawgeti(state, index, slot) lua_rawgeti(state, index, static_cast<int>(slot))
#define lua_rawseti(state, index, slot) lua_rawseti(state, index, static_cast<int>(slot))

// Lua 5.2's absolute stack index.
inline int kb_lua_absindex(lua_State* state, int index) {
    return index > 0 || index <= LUA_REGISTRYINDEX ? index : lua_gettop(state) + index + 1;
}
#define lua_absindex(state, index) kb_lua_absindex(state, index)

// luaL_tolstring of Lua 5.2 and later: the text of any value, through __tostring when it has one.
inline const char* kb_lua_tolstring(lua_State* state, int index, std::size_t* length) {
    if (luaL_callmeta(state, index, "__tostring") != 0) {
        if (lua_isstring(state, -1) == 0) luaL_error(state, "'__tostring' must return a string");
    } else {
        switch (lua_type(state, index)) {
        case LUA_TNUMBER:
        case LUA_TSTRING:
            lua_pushvalue(state, index);
            break;
        case LUA_TBOOLEAN:
            lua_pushstring(state, lua_toboolean(state, index) != 0 ? "true" : "false");
            break;
        case LUA_TNIL:
            lua_pushliteral(state, "nil");
            break;
        default:
            lua_pushfstring(state, "%s: %p", luaL_typename(state, index), lua_topointer(state, index));
            break;
        }
    }
    return lua_tolstring(state, -1, length);
}
#define luaL_tolstring(state, index, length) kb_lua_tolstring(state, index, length)

// A registry slot keyed by the address of a variable: the key is a light userdata.
inline int kb_lua_rawgetp(lua_State* state, int index, const void* key) {
    index = kb_lua_absindex(state, index);
    lua_pushlightuserdata(state, const_cast<void*>(key));
    lua_rawget(state, index);
    return lua_type(state, -1);
}
inline void kb_lua_rawsetp(lua_State* state, int index, const void* key) {
    index = kb_lua_absindex(state, index);
    lua_pushlightuserdata(state, const_cast<void*>(key));
    lua_insert(state, -2);
    lua_rawset(state, index);
}
#define lua_rawgetp(state, index, key) kb_lua_rawgetp(state, index, key)
#define lua_rawsetp(state, index, key) kb_lua_rawsetp(state, index, key)

// Lua 5.4's resume takes the resuming thread and reports the number of results; LuaJIT leaves them on the stack.
inline int kb_lua_resume(lua_State* thread, lua_State* /*from*/, int argumentCount, int* resultCount) {
    const int status = lua_resume(thread, argumentCount);
    *resultCount = status == 0 || status == LUA_YIELD ? lua_gettop(thread) : 0;
    return status;
}
#define lua_resume(thread, from, argumentCount, resultCount) kb_lua_resume(thread, from, argumentCount, resultCount)

// LuaJIT runs a chunk's globals from its function environment where Lua 5.4 uses the first upvalue (_ENV). Takes the
// table on top of the stack as the environment of the chunk below it and pops it.
inline void kb_lua_set_chunk_environment(lua_State* state) {
    static_cast<void>(lua_setfenv(state, -2));
}

// Pushes the environment table of the function at `functionIndex`; false (nothing pushed) when it has none.
inline bool kb_lua_push_function_environment(lua_State* state, int functionIndex) {
    lua_getfenv(state, functionIndex);
    if (lua_istable(state, -1) != 0) return true;
    lua_pop(state, 1);
    return false;
}

// Compiled code lives outside the state's heap, so a heap image cannot carry it: the compiler's traces are dropped before
// a heap is captured and before one is put back, and compiled again as the scripts run on.
inline void kb_lua_flush_compiled_code(lua_State* state) noexcept {
    static_cast<void>(luaJIT_setmode(state, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_FLUSH));
}

#else

// Lua 5.4.
inline void kb_lua_set_chunk_environment(lua_State* state) {
    static_cast<void>(lua_setupvalue(state, -2, 1));
}

inline bool kb_lua_push_function_environment(lua_State* state, int functionIndex) {
    functionIndex = lua_absindex(state, functionIndex);
    for (int upvalueIndex = 1;; ++upvalueIndex) {
        const char* name = lua_getupvalue(state, functionIndex, upvalueIndex);
        if (name == nullptr) return false;
        if (std::string_view{ name } == "_ENV" && lua_istable(state, -1) != 0) return true;
        lua_pop(state, 1);
    }
}

inline void kb_lua_flush_compiled_code(lua_State* /*state*/) noexcept {}

#endif
