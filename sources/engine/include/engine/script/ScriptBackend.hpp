#pragma once

#include "engine/scene/BehaviourComponent.hpp"
#include "engine/script/ScriptEvent.hpp"
#include "engine/script/ScriptExecutionContext.hpp"
#include "engine/script/ScriptLifecycle.hpp"

#include <optional>
#include <string>
#include <vector>

namespace kb::script {

// LIB-036: module/function/lifecyclePhase are optional (empty string /
// nullopt) because most diagnostics today are produced deep inside a
// backend that only knows entity/asset/backend — callers with the fuller
// context (kb::library's future domain modules, ScriptFunctionRegistry
// call sites) populate them; a diagnostic missing them is exactly as valid
// as before this field was added, never a stub.
struct ScriptDiagnostic {
    kb::scene::SceneEntity entity{};
    kb::assets::AssetId assetId{};
    kb::scene::BehaviourBackend backend = kb::scene::BehaviourBackend::Native;
    std::string module;
    std::string function;
    std::optional<ScriptLifecycleEvent> lifecyclePhase;
    std::string message;
};

struct ScriptBackendExecutionResult {
    bool executed = false;
    std::vector<ScriptDiagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept {
        return diagnostics.empty();
    }
};

class IScriptBackend {
public:
    virtual ~IScriptBackend() = default;

    [[nodiscard]] virtual kb::scene::BehaviourBackend Backend() const noexcept = 0;
    [[nodiscard]] virtual ScriptBackendExecutionResult ExecuteLifecycle(
        const kb::scene::BehaviourComponent& behaviour,
        ScriptExecutionContext& context) = 0;
    // LIB-104: eventId is ScriptEvent::Id(), computed ONCE per dispatch by
    // ScriptRuntime::DispatchEvent (not once per behaviour) — the typed key
    // NativeScriptBackend uses for its hot-path callback lookup instead of
    // rebuilding a string from `event.name` on every call. Lua/Visual Graph
    // backends accept it but do not use it: their own dispatch is
    // inherently string-keyed (Lua global lookup, Visual Graph node
    // matching) — rearchitecting either is out of scope here, the same
    // proportionality precedent as LIB-097/098's documented deferrals.
    [[nodiscard]] virtual ScriptBackendExecutionResult ExecuteEvent(
        const kb::scene::BehaviourComponent& behaviour,
        const ScriptEvent& event,
        EventId eventId,
        ScriptExecutionContext& context) = 0;
};

} // namespace kb::script
