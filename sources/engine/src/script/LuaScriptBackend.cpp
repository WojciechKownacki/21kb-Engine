#include "engine/script/LuaScriptBackend.hpp"

namespace kb::script {

LuaScriptBackend::LuaScriptBackend(ILuaScriptRuntime& runtime) noexcept
    : runtime_(runtime) {}

kb::scene::BehaviourBackend LuaScriptBackend::Backend() const noexcept {
    return kb::scene::BehaviourBackend::Lua;
}

ScriptBackendExecutionResult LuaScriptBackend::ExecuteLifecycle(const kb::scene::BehaviourComponent& behaviour, ScriptExecutionContext& context) {
    return runtime_.ExecuteLifecycle(behaviour, context);
}

ScriptBackendExecutionResult LuaScriptBackend::ExecuteEvent(const kb::scene::BehaviourComponent& behaviour, const ScriptEvent& event, ScriptExecutionContext& context) {
    return runtime_.ExecuteEvent(behaviour, event, context);
}

} // namespace kb::script
