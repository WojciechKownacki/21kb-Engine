#pragma once

struct lua_State;

namespace kb::script {

// A raw Lua-callable C function, matching PUC-Lua's own `lua_CFunction`
// typedef (`int (*)(lua_State*)`) — named here so callers of this header
// never need to include <lua.h> themselves (the same discipline
// PucLuaScriptRuntime.hpp already follows with a forward-declared
// `lua_State`).
using PucLuaRawFunction = int (*)(lua_State*);

// LIB-010: every lua_CFunction this engine registers with a lua_State
// (lua_pushcclosure/lua_pushcfunction) must be reached only through this
// function or the template below — never registered directly. PUC-Lua is
// compiled as plain C and uses setjmp/longjmp internally for its own error
// handling (lua_pcall/lua_error); a C++ exception thrown by a registered
// function (or by any engine code it calls) that tries to unwind across
// PUC-Lua's C call frames is undefined behaviour, not a diagnosable
// failure — ScriptFunctionRegistry::Call already closes this for calls
// routed through it (Native, Lua's CallFunction, the future Visual Graph
// CallNative node), but the ~150 Lua "sugar" functions bound directly by
// PucLuaFunctionApi/PucLuaSelfApi/PucLuaSharedApi/PucLuaEventApi/
// PucLuaEventsApi/PucLuaModuleApi call engine code before that choke point
// is ever reached (argument marshalling) or bypass it entirely (Self.*,
// Shared.*, Events.*, Import). PucLuaSafeInvoke/PucLuaSafeCall close that
// gap uniformly: the exception is fully caught and converted to a Lua
// error (luaL_error, itself a longjmp) *before* any unwinding across
// PUC-Lua's frames is required — the standard, correct pattern for
// bridging C++ exceptions across a longjmp-based C API.
[[nodiscard]] int PucLuaSafeInvoke(lua_State* state, PucLuaRawFunction function);

// Compile-time form for registration call sites that already know their
// function pointer at compile time (the common case:
// lua_pushcclosure(state, &PucLuaSafeCall<&LuaSomething>, upvalueCount)) —
// a distinct trampoline per instantiation, calling Function(state) directly
// so Function's own lua_upvalueindex(N) reads are unaffected (Lua's
// "currently running closure" is still this trampoline's own frame, with
// exactly the upvalues the call site pushed).
template <PucLuaRawFunction Function>
int PucLuaSafeCall(lua_State* state) {
    return PucLuaSafeInvoke(state, Function);
}

} // namespace kb::script
