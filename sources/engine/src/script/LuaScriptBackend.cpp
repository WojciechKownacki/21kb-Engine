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

ScriptBackendExecutionResult LuaScriptBackend::ExecuteEvent(const kb::scene::BehaviourComponent& behaviour, const ScriptEvent& event, EventId eventId, ScriptExecutionContext& context) {
    return runtime_.ExecuteEvent(behaviour, event, eventId, context);
}

void LuaScriptBackend::ApplyExposedVariableOverrides(
    kb::scene::SceneEntity entity,
    const kb::scene::BehaviourComponent& behaviour,
    std::span<const kb::scene::BehaviourVariableOverride> overrides) {
    const kb::assets::AssetId assetId{ behaviour.behaviourAssetId };
    for (const kb::scene::BehaviourVariableOverride& entry : overrides) {
        runtime_.SetInstanceVariableOverride(entity, assetId, entry.name, entry.value);
    }
}

bool LuaScriptBackend::HasEntry(const kb::scene::BehaviourComponent& behaviour, std::string_view name) {
    return runtime_.HasEntry(behaviour, name);
}

ScriptPhaseCall LuaScriptBackend::ResolvePhaseCall(
    kb::scene::SceneEntity entity,
    const kb::scene::BehaviourComponent& behaviour,
    ScriptLifecycleEvent event) {
    ScriptPhaseCall call;
    if (runtime_.ResolvePhaseCall(entity, behaviour, event, call)) {
        return call;
    }
    return IScriptBackend::ResolvePhaseCall(entity, behaviour, event);
}

std::uint64_t LuaScriptBackend::PhaseCallRevision() const noexcept {
    return runtime_.PhaseCallRevision();
}

void LuaScriptBackend::BeginEventBroadcast(const ScriptEvent& event) {
    runtime_.BeginEventBroadcast(event);
}

void LuaScriptBackend::EndEventBroadcast() noexcept {
    runtime_.EndEventBroadcast();
}

void LuaScriptBackend::ResetAssetForHotReload(kb::assets::AssetId assetId, ScriptEventBus& events) noexcept {
    runtime_.ResetAssetForHotReload(assetId, events);
}

} // namespace kb::script
