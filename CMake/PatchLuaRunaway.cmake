# Adds the runaway-script allowance to the fetched Lua sources. Run by the kb_lua FetchContent as its patch step:
#   cmake -DLUA_SOURCE_DIR=<lua>/src -P PatchLuaRunaway.cmake
#
# The interpreter charges one unit to a global allowance for every backward jump (while, repeat, goto, numeric and generic
# for) and every call. When the allowance is spent it arms the hook the host registered with lua_setrunaway on the running
# thread, so the host's hook ends or suspends the script from an ordinary count-hook event. Nothing is executed per
# instruction, unlike a count hook. Applying the script twice changes nothing.

if(NOT LUA_SOURCE_DIR)
    message(FATAL_ERROR "PatchLuaRunaway.cmake needs -DLUA_SOURCE_DIR=<lua source directory>")
endif()

# Already patched (the marker is in the first file the script edits): nothing to do.
file(READ "${LUA_SOURCE_DIR}/lstate.h" lstate_text)
if(lstate_text MATCHES "KB_RUNAWAY")
    return()
endif()

function(kb_lua_replace name from to)
    set(path "${LUA_SOURCE_DIR}/${name}")
    file(READ "${path}" text)
    string(FIND "${text}" "${from}" first)
    if(first EQUAL -1)
        message(FATAL_ERROR "Lua runaway patch: expected text not found in ${name}:\n${from}")
    endif()
    string(REPLACE "${from}" "${to}" text "${text}")
    file(WRITE "${path}" "${text}")
endfunction()

kb_lua_replace(lstate.c
    "  g->panic = NULL;\n"
    "  g->panic = NULL;\n  g->runawaycount = MAX_INT;  /* KB_RUNAWAY */\n  g->runawayhook = NULL;\n")

kb_lua_replace(lua.h
    "LUA_API int (lua_gethookcount) (lua_State *L);\n"
    "LUA_API int (lua_gethookcount) (lua_State *L);\nLUA_API void (lua_setrunaway) (lua_State *L, lua_Hook hook, int limit);  /* KB_RUNAWAY */\nLUA_API int (lua_getrunaway) (lua_State *L);\n")

kb_lua_replace(ldebug.h
    "LUAI_FUNC int luaG_traceexec (lua_State *L, const Instruction *pc);\n"
    "LUAI_FUNC int luaG_traceexec (lua_State *L, const Instruction *pc);\nLUAI_FUNC void luaG_runaway (lua_State *L);  /* KB_RUNAWAY */\n")

kb_lua_replace(ldebug.c
    "LUA_API lua_Hook lua_gethook (lua_State *L) {\n"
    "/* KB_RUNAWAY: with a hook the allowance counts down from 'limit' (zero or no hook: effectively unlimited). */
LUA_API void lua_setrunaway (lua_State *L, lua_Hook hook, int limit) {
  global_State *g = G(L);
  g->runawayhook = hook;
  g->runawaycount = (hook == NULL || limit <= 0) ? MAX_INT : limit;
}


LUA_API int lua_getrunaway (lua_State *L) {
  return G(L)->runawaycount;
}


/*
** The allowance ran out: count-hook the running thread so the host's hook sees the next instruction. The allowance stays
** spent, so a thread that catches the error is stopped again at its next jump or call.
*/
void luaG_runaway (lua_State *L) {
  global_State *g = G(L);
  g->runawaycount = 0;
  if (g->runawayhook == NULL) {  /* no longer armed */
    g->runawaycount = MAX_INT;
    return;
  }
  if (L->hook != g->runawayhook || !(L->hookmask & LUA_MASKCOUNT) || L->basehookcount != 1)
    lua_sethook(L, g->runawayhook, L->hookmask | LUA_MASKCOUNT, 1);
}


LUA_API lua_Hook lua_gethook (lua_State *L) {\n")

kb_lua_replace(ldo.c
    "int luaD_pretailcall (lua_State *L, CallInfo *ci, StkId func,\n                                    int narg1, int delta) {\n retry:\n"
    "int luaD_pretailcall (lua_State *L, CallInfo *ci, StkId func,\n                                    int narg1, int delta) {\n  if (l_unlikely(--G(L)->runawaycount <= 0)) luaG_runaway(L);  /* KB_RUNAWAY */\n retry:\n")

kb_lua_replace(ldo.c
    "CallInfo *luaD_precall (lua_State *L, StkId func, int nresults) {\n retry:\n"
    "CallInfo *luaD_precall (lua_State *L, StkId func, int nresults) {\n  if (l_unlikely(--G(L)->runawaycount <= 0)) luaG_runaway(L);  /* KB_RUNAWAY */\n retry:\n")

kb_lua_replace(lvm.c
    "#define donextjump(ci)\t{ Instruction ni = *pc; dojump(ci, ni, 1); }"
    "#define donextjump(ci)\t{ Instruction ni = *pc; if (GETARG_sJ(ni) < 0) runawaycheck(); dojump(ci, ni, 1); }")

kb_lua_replace(lvm.c
    "#define ProtectNT(exp)  (savepc(L), (exp), updatetrap(ci))\n"
    "#define ProtectNT(exp)  (savepc(L), (exp), updatetrap(ci))\n\n/* KB_RUNAWAY: charge one backward jump to the allowance; spending it arms the host's hook */\n#define runawaycheck()  { if (l_unlikely(--G(L)->runawaycount <= 0)) Protect(luaG_runaway(L)); }\n")

kb_lua_replace(lvm.c
    "      vmcase(OP_JMP) {\n        dojump(ci, i, 0);\n"
    "      vmcase(OP_JMP) {\n        if (GETARG_sJ(i) < 0) runawaycheck();\n        dojump(ci, i, 0);\n")

kb_lua_replace(lvm.c
    "            setivalue(s2v(ra + 3), idx);  /* and control variable */\n            pc -= GETARG_Bx(i);  /* jump back */\n"
    "            setivalue(s2v(ra + 3), idx);  /* and control variable */\n            pc -= GETARG_Bx(i);  /* jump back */\n            runawaycheck();\n")

kb_lua_replace(lvm.c
    "        else if (floatforloop(ra))  /* float loop */\n          pc -= GETARG_Bx(i);  /* jump back */\n"
    "        else if (floatforloop(ra)) {  /* float loop */\n          pc -= GETARG_Bx(i);  /* jump back */\n          runawaycheck();\n        }\n")

kb_lua_replace(lvm.c
    "          setobjs2s(L, ra + 2, ra + 4);  /* save control variable */\n          pc -= GETARG_Bx(i);  /* jump back */\n"
    "          setobjs2s(L, ra + 2, ra + 4);  /* save control variable */\n          pc -= GETARG_Bx(i);  /* jump back */\n          runawaycheck();\n")

# The marker is written last: a run that stops with an error leaves the tree unmarked and half edited, so fetch it again.
kb_lua_replace(lstate.h
    "typedef struct global_State {\n"
    "typedef struct global_State {\n  int runawaycount;  /* KB_RUNAWAY: loop iterations and calls left before the hook below is armed */\n  lua_Hook runawayhook;  /* KB_RUNAWAY: hook armed on the running thread when the allowance is spent */\n")
