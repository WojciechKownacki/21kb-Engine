#pragma once

#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/BehaviourVariableOverride.hpp"
#include "engine/script/ScriptEvent.hpp"
#include "engine/script/ScriptExecutionContext.hpp"
#include "engine/script/ScriptLifecycle.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kb::script {

class ScriptEventBus;

// LIB-036: module/function/lifecyclePhase are optional (empty string /
// nullopt) because most diagnostics today are produced deep inside a
// backend that only knows entity/asset/backend — callers with the fuller
// context (kb::library modules, ScriptFunctionRegistry
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
    std::string stackTrace;
};

struct ScriptBackendExecutionResult {
    bool executed = false;
    std::vector<ScriptDiagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept {
        return diagnostics.empty();
    }
};

// One behaviour's resolved call for a per-frame phase: what the phase loops run every frame without looking anything
// up. `invoke` runs it and says whether it ran and succeeded; a failure appends its diagnostics. A null `invoke` means
// the behaviour has nothing to run in the phase.
struct ScriptPhaseCall {
    using Invoke = bool (*)(
        void* target,
        const kb::scene::BehaviourComponent& behaviour,
        ScriptExecutionContext& context,
        std::vector<ScriptDiagnostic>& diagnostics);

    Invoke invoke = nullptr;
    void* target = nullptr;
    // The call only changes its own object (see NativeScriptClassApi::runOnAnyThread): a phase may run it on a
    // worker thread together with other such calls.
    bool runOnAnyThread = false;
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

    // Seed the editor-authored per-instance overrides of this behaviour's
    // exposed ("@expose") variables into the backend's live instance state,
    // called once just before the behaviour's Created lifecycle so GetVariable
    // returns the authored value from the first frame. Default no-op: only the
    // Lua backend supports exposed variables today (Native/VisualGraph ignore).
    virtual void ApplyExposedVariableOverrides(
        kb::scene::SceneEntity entity,
        const kb::scene::BehaviourComponent& behaviour,
        std::span<const kb::scene::BehaviourVariableOverride> overrides) {
        static_cast<void>(entity);
        static_cast<void>(behaviour);
        static_cast<void>(overrides);
    }

    // Whether `behaviour` has a function called `name` to run - a per-frame phase (ToString of FixedTick .. AfterRender)
    // or an event handler. The dispatchers ask once per script asset and name and pass over the instances of an asset
    // that answers no, so a script that defines only some callbacks costs nothing in the others. A backend that cannot
    // tell answers yes.
    [[nodiscard]] virtual bool HasEntry(
        const kb::scene::BehaviourComponent& behaviour,
        std::string_view name) {
        static_cast<void>(behaviour);
        static_cast<void>(name);
        return true;
    }

    // An event that reaches several behaviours (no target) is handed to them one after another between these two calls,
    // so a backend that builds a value for the event can build it once for all receivers instead of once per receiver.
    // The event outlives the broadcast; a backend that does not care keeps the defaults.
    virtual void BeginEventBroadcast(const ScriptEvent& event) {
        static_cast<void>(event);
    }
    virtual void EndEventBroadcast() noexcept {}

    // The call the phase loops make for `behaviour` in `event` (FixedTick .. AfterRender), found once and kept in their
    // dispatch lists until PhaseCallRevision moves or the behaviour changes. The default runs it through
    // ExecuteLifecycle, which is right for a backend that has nothing to resolve.
    [[nodiscard]] virtual ScriptPhaseCall ResolvePhaseCall(
        kb::scene::SceneEntity entity,
        const kb::scene::BehaviourComponent& behaviour,
        ScriptLifecycleEvent event) {
        static_cast<void>(entity);
        static_cast<void>(behaviour);
        static_cast<void>(event);
        return ScriptPhaseCall{ &IScriptBackend::InvokeThroughExecuteLifecycle, this };
    }

    // The call a broadcast of the event called `name` makes for `behaviour` (the event reaches it as the context's
    // IncomingEvent), kept in the runtime's receiver list of that event as the phase loops keep ResolvePhaseCall's
    // calls, and under the same PhaseCallRevision. The default runs it through ExecuteEvent.
    [[nodiscard]] virtual ScriptPhaseCall ResolveEventCall(
        kb::scene::SceneEntity entity,
        const kb::scene::BehaviourComponent& behaviour,
        std::string_view name) {
        static_cast<void>(entity);
        static_cast<void>(behaviour);
        static_cast<void>(name);
        return ScriptPhaseCall{ &IScriptBackend::InvokeThroughExecuteEvent, this };
    }

    // Moves whenever a call ResolvePhaseCall or ResolveEventCall returned may no longer be the right one (a registration or a reload
    // changed, an object was made or destroyed); the dispatch lists are built again when it differs from the value they
    // were built with.
    [[nodiscard]] virtual std::uint64_t PhaseCallRevision() const noexcept {
        return 0U;
    }

    // Releases state owned by live instances of one behaviour asset after a
    // successful source/artifact replacement. Hot reload intentionally does
    // not invoke Deactivated/Destroyed: those callbacks would execute the new
    // implementation against state created by the old one. The scene system
    // then enters Created/Activated/Ready at its next synchronization point.
    virtual void ResetAssetForHotReload(
        kb::assets::AssetId assetId,
        ScriptEventBus& events) noexcept {
        static_cast<void>(assetId);
        static_cast<void>(events);
    }

protected:
    [[nodiscard]] static bool InvokeThroughExecuteLifecycle(
        void* target,
        const kb::scene::BehaviourComponent& behaviour,
        ScriptExecutionContext& context,
        std::vector<ScriptDiagnostic>& diagnostics) {
        ScriptBackendExecutionResult result = static_cast<IScriptBackend*>(target)->ExecuteLifecycle(behaviour, context);
        for (ScriptDiagnostic& diagnostic : result.diagnostics) {
            diagnostics.push_back(std::move(diagnostic));
        }
        return result.executed && result.diagnostics.empty();
    }
    [[nodiscard]] static bool InvokeThroughExecuteEvent(
        void* target,
        const kb::scene::BehaviourComponent& behaviour,
        ScriptExecutionContext& context,
        std::vector<ScriptDiagnostic>& diagnostics) {
        const ScriptEvent& event = *context.IncomingEvent();
        ScriptBackendExecutionResult result = static_cast<IScriptBackend*>(target)->ExecuteEvent(behaviour, event, event.Id(), context);
        for (ScriptDiagnostic& diagnostic : result.diagnostics) {
            diagnostics.push_back(std::move(diagnostic));
        }
        return result.executed && result.diagnostics.empty();
    }
};

} // namespace kb::script
