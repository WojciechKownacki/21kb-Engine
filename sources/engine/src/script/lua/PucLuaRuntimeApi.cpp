#include "script/lua/PucLuaRuntimeApi.hpp"

#include "engine/script/PucLuaScriptRuntime.hpp"
#include "script/lua/api/PucLuaEventApi.hpp"
#include "script/lua/api/PucLuaEventsApi.hpp"
#include "script/lua/api/PucLuaFunctionApi.hpp"
#include "script/lua/api/PucLuaModuleApi.hpp"
#include "script/lua/api/PucLuaSelfApi.hpp"
#include "script/lua/api/PucLuaSharedApi.hpp"

namespace kb::script {

void PucLuaRuntimeApi::AttachRuntimeFunctions(lua_State* state, int environmentIndex, PucLuaScriptRuntime& runtime) {
    PucLuaModuleApi::AttachImport(state, environmentIndex, runtime);
    // Attach `Inspector` with a null context so top-level `Inspector.x = default`
    // declarations run harmlessly at chunk load (the schema is parsed statically).
    PucLuaSelfApi::AttachInspector(state, environmentIndex, nullptr);
}

void PucLuaRuntimeApi::AttachExecutionApi(lua_State* state, int environmentIndex, ScriptExecutionContext& context, PucLuaScriptRuntime& runtime) {
    AttachRuntimeFunctions(state, environmentIndex, runtime);
    PucLuaEventApi::Attach(state, environmentIndex, context);
    PucLuaEventsApi::Attach(state, environmentIndex, context, runtime);
    PucLuaSharedApi::Attach(state, environmentIndex, context);
    PucLuaFunctionApi::Attach(state, environmentIndex, context);
    // Re-attach `Inspector` bound to THIS execution's context so `Inspector.x`
    // read/write routes to the current entity's exposed-variable instance.
    PucLuaSelfApi::AttachInspector(state, environmentIndex, &context);
}

void PucLuaRuntimeApi::PushSelf(lua_State* state, ScriptExecutionContext& context) {
    PucLuaSelfApi::PushSelf(state, context);
}

void PucLuaRuntimeApi::PushEvent(lua_State* state, const ScriptEvent& event) {
    PucLuaEventApi::PushEvent(state, event);
}

} // namespace kb::script
