#pragma once

struct lua_State;

namespace kb::script {

class ScriptExecutionContext;

class PucLuaSelfApi final {
public:
    static void PushSelf(lua_State* state, ScriptExecutionContext& context);
    // Installs the engine-provided `Inspector` global table into the environment.
    // `Inspector.name = value` at chunk top level (context == nullptr) is a no-op
    // proxy (the schema is parsed statically from the source), so declarations do
    // not error at load; during execution (context != nullptr) every read/write of
    // `Inspector.name` routes to the CURRENT entity's exposed-variable instance
    // (per-entity instance semantics), mirroring Self:Get/SetVariable.
    static void AttachInspector(lua_State* state, int environmentIndex, ScriptExecutionContext* context);
};

} // namespace kb::script
