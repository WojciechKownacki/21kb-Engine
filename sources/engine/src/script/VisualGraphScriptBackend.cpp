#include "engine/script/VisualGraphScriptBackend.hpp"

#include "engine/scene/SceneComponents.hpp"
#include "engine/visual/VisualGraphBehaviourRuntime.hpp"
#include "engine/visual/VisualGraphDiagnostic.hpp"

#include <utility>

namespace kb::script {
namespace {

[[nodiscard]] ScriptValue ToScriptValue(const kb::visual::VisualGraphRuntimeValue& value) {
    switch (value.Type()) {
    case kb::visual::VisualGraphValueType::Bool:
        return ScriptValue{value.AsBool()};
    case kb::visual::VisualGraphValueType::Int:
        return ScriptValue{value.AsInt()};
    case kb::visual::VisualGraphValueType::Float:
        return ScriptValue{value.AsFloat()};
    case kb::visual::VisualGraphValueType::String:
        return ScriptValue{value.AsString()};
    case kb::visual::VisualGraphValueType::Entity:
        return ScriptValue{value.AsUInt64(), ScriptValueType::Entity};
    case kb::visual::VisualGraphValueType::Component:
        return ScriptValue{value.AsUInt64(), ScriptValueType::Component};
    case kb::visual::VisualGraphValueType::UInt32:
        return ScriptValue{static_cast<std::uint32_t>(value.AsUInt64())};
    case kb::visual::VisualGraphValueType::Hash:
        return ScriptValue{value.AsUInt64(), ScriptValueType::Hash};
    case kb::visual::VisualGraphValueType::Int64:
        return ScriptValue{value.AsInt64()};
    case kb::visual::VisualGraphValueType::Double:
        return ScriptValue{value.AsDouble()};
    case kb::visual::VisualGraphValueType::Name:
        return ScriptValue{value.AsString(), ScriptValueType::Name};
    case kb::visual::VisualGraphValueType::Guid:
        return ScriptValue{value.AsString(), ScriptValueType::Guid};
    case kb::visual::VisualGraphValueType::Void:
        break;
    }
    return ScriptValue{};
}

[[nodiscard]] std::vector<ScriptEventArgument> ToScriptArguments(const std::vector<kb::visual::VisualGraphEventArgument>& arguments) {
    std::vector<ScriptEventArgument> result;
    result.reserve(arguments.size());
    for (const kb::visual::VisualGraphEventArgument& argument : arguments) {
        result.push_back(ScriptEventArgument{
            .name = argument.name,
            .value = ToScriptValue(argument.value),
        });
    }
    return result;
}

} // namespace

VisualGraphScriptBackend::VisualGraphScriptBackend(
    const kb::visual::VisualGraphRuntimeRegistry& artifacts,
    const kb::visual::VisualGraphRuntimeBindingRegistry& bindings,
    kb::visual::VisualGraphBehaviourInstanceRegistry& instances) noexcept
    : artifacts_(artifacts)
    , bindings_(bindings)
    , instances_(instances) {}

kb::scene::BehaviourBackend VisualGraphScriptBackend::Backend() const noexcept {
    return kb::scene::BehaviourBackend::VisualGraph;
}

ScriptBackendExecutionResult VisualGraphScriptBackend::ExecuteLifecycle(const kb::scene::BehaviourComponent& behaviour, ScriptExecutionContext& context) {
    kb::visual::VisualGraphRuntimeExecutionContext& graphContext = ContextFor(behaviour, context.Self());
    StoreCommonInputs(graphContext, context);
    const kb::visual::VisualGraphBehaviourExecutionResult result = kb::visual::VisualGraphBehaviourRuntime::Execute(
        behaviour,
        context.Self(),
        ToVisualGraphLifecycleEvent(context.Lifecycle()),
        artifacts_,
        bindings_,
        graphContext);
    AppendEmittedEvents(graphContext, context, context.Self(), kb::assets::AssetId{behaviour.behaviourAssetId});
    // LIB-112: the gameplay event bridge's RECEIVE side is wired to the
    // SAME lifecycle boundaries every other owned resource in this engine
    // uses (Timer/Task/LIB-095/097's owner-entity pattern) — Subscribe
    // once, exactly on Created (never re-fires on Activated/Deactivated,
    // ScriptLifecycleEvent's own ordering), Unsubscribe once on Destroyed.
    if (context.Lifecycle() == ScriptLifecycleEvent::Created) {
        SubscribeCustomEventsToBus(behaviour, context);
    } else if (context.Lifecycle() == ScriptLifecycleEvent::Destroyed) {
        UnsubscribeCustomEventsFromBus(kb::assets::AssetId{behaviour.behaviourAssetId}, context.Self(), context.Events());
    }
    return ToScriptResult(result, behaviour, context.Self());
}

ScriptBackendExecutionResult VisualGraphScriptBackend::ExecuteEvent(const kb::scene::BehaviourComponent& behaviour, const ScriptEvent& event, EventId /*eventId*/, ScriptExecutionContext& context) {
    // LIB-104: Visual Graph resolves the custom-event handler by matching
    // event.name against compiled node names (VisualGraphBehaviourRuntime::
    // ExecuteCustomEvent) — inherently string-keyed by the graph runtime
    // itself, so the typed EventId has no lookup to replace here (see
    // IScriptBackend::ExecuteEvent's comment).
    kb::visual::VisualGraphRuntimeExecutionContext& graphContext = ContextFor(behaviour, context.Self());
    StoreCommonInputs(graphContext, context);
    const std::vector<kb::visual::VisualGraphCustomEventArgument> arguments = ToVisualGraphArguments(event);
    const kb::visual::VisualGraphBehaviourExecutionResult result = kb::visual::VisualGraphBehaviourRuntime::ExecuteCustomEvent(
        behaviour,
        context.Self(),
        event.name,
        arguments,
        artifacts_,
        bindings_,
        graphContext);
    AppendEmittedEvents(graphContext, context, context.Self(), kb::assets::AssetId{behaviour.behaviourAssetId});
    return ToScriptResult(result, behaviour, context.Self());
}

kb::visual::VisualGraphRuntimeExecutionContext& VisualGraphScriptBackend::ContextFor(const kb::scene::BehaviourComponent& behaviour, kb::scene::SceneEntity entity) {
    return instances_.FindOrCreate(entity, kb::assets::AssetId{behaviour.behaviourAssetId}).context;
}

void VisualGraphScriptBackend::StoreCommonInputs(kb::visual::VisualGraphRuntimeExecutionContext& context, const ScriptExecutionContext& scriptContext) const {
    context.Store(0U, "self", kb::visual::VisualGraphRuntimeValue{scriptContext.Self().Id(), kb::visual::VisualGraphValueType::Entity});
    context.Store(0U, "deltaSeconds", kb::visual::VisualGraphRuntimeValue{scriptContext.DeltaSeconds()});
}

void VisualGraphScriptBackend::AppendEmittedEvents(
    kb::visual::VisualGraphRuntimeExecutionContext& graphContext,
    ScriptExecutionContext& scriptContext,
    kb::scene::SceneEntity sender,
    kb::assets::AssetId assetId) const {
    for (const kb::visual::VisualGraphEmittedEvent& event : graphContext.EmittedEventRecords()) {
        if (event.target.IsValid()) {
            scriptContext.EmitTo(event.target, event.name, ToScriptArguments(event.arguments));
        } else {
            scriptContext.Emit(event.name, ToScriptArguments(event.arguments));
        }
    }
    // LIB-112: ALSO broadcast the SAME emitted events through ScriptEventBus
    // (LIB-105) — additive, never a replacement for the loop above (which
    // stays exactly as it was before this task). See
    // BroadcastEmittedEventsToBus's own doc comment for why.
    if (ScriptEventBus* eventBus = scriptContext.Events(); eventBus != nullptr) {
        BroadcastEmittedEventsToBus(graphContext, scriptContext.GetScene(), *eventBus);
    }
    static_cast<void>(sender);
    static_cast<void>(assetId);
}

// LIB-112: EMIT direction of the gameplay event bridge. Every event a
// Visual Graph EmitEvent node produces (already typed-pin authored by the
// graph — name, target, and a set of named+typed arguments, exactly the
// same VisualGraphEmittedEvent shape AppendEmittedEvents already forwards
// through the old DispatchEvent mechanism above) ALSO reaches the bus:
// `target` invalid maps to an untargeted Emit (equivalent to Broadcast —
// Broadcast IS Emit with an invalid target, ScriptEventBus.cpp), `target`
// valid maps to a targeted Emit reaching only that entity's bus
// subscriptions — the graph author's existing "emit to everyone" vs "emit
// to this entity" choice on the EmitEvent node carries over unchanged.
// This means a native Events.Subscribe listener, a Lua Events.Subscribe
// listener, or another Visual Graph behaviour's bus-subscribed CustomEvent
// node (SubscribeCustomEventsToBus below) now all receive events a graph
// emits — closing the real gap ScriptEventBus.hpp's own doc comment named
// this task for.
void VisualGraphScriptBackend::BroadcastEmittedEventsToBus(
    kb::visual::VisualGraphRuntimeExecutionContext& graphContext,
    kb::scene::Scene& scene,
    ScriptEventBus& eventBus) const {
    for (const kb::visual::VisualGraphEmittedEvent& event : graphContext.EmittedEventRecords()) {
        ScriptEvent scriptEvent;
        scriptEvent.name = event.name;
        scriptEvent.target = event.target;
        scriptEvent.arguments = ToScriptArguments(event.arguments);
        static_cast<void>(eventBus.Emit(scene, scriptEvent, event.target));
    }
}

// LIB-112: RECEIVE direction of the gameplay event bridge. On a
// behaviour's Created lifecycle (fires exactly once — never re-fires on
// Activated/Deactivated, kb::script::ScriptLifecycleEvent's own ordering),
// enumerate every CustomEvent node the COMPILED graph defines
// (VisualGraphIrFunction::customEventName, populated by VisualGraphCompiler.
// cpp from whatever pins the asset author hand-typed on that node — the
// SAME typed pins StoreCustomEventArguments already type-checks against
// for the old DispatchEvent path) and Events.Subscribe (LIB-105) to each
// one, on this behaviour's own entity.
//
// The callback below is a NATIVE C++ closure WE construct — this is HOW a
// typed-pin bridge is possible at all despite ScriptValue having no
// callable type (ScriptEventBus.hpp's own documented reason Visual Graph
// was excluded until this task): the graph itself never authors or holds
// a callback, which it structurally cannot; the engine wires the
// subscription on the graph's behalf, entirely invisible to the graph
// author, who just sees their existing CustomEvent node start receiving
// events from a second, real source.
//
// `owner = entity`: a dead/deactivated owner is silently auto-skipped by
// ScriptEventBus's own OwnerGone policy (LIB-095/097/099) even before
// UnsubscribeCustomEventsFromBus's explicit cleanup below runs — belt and
// suspenders, not a substitute for it (an unsubscribed-but-still-alive
// entry would otherwise sit in the bus's subscription list until the next
// unrelated Emit of that same name happens to sweep it).
void VisualGraphScriptBackend::SubscribeCustomEventsToBus(const kb::scene::BehaviourComponent& behaviour, ScriptExecutionContext& context) {
    ScriptEventBus* eventBus = context.Events();
    if (eventBus == nullptr) {
        return;
    }
    const kb::assets::AssetId assetId{behaviour.behaviourAssetId};
    const kb::visual::VisualGraphRuntimeArtifact* artifact = artifacts_.Find(assetId);
    if (artifact == nullptr) {
        return;
    }
    const kb::scene::SceneEntity entity = context.Self();
    const EventBridgeKey key{entity.Id(), assetId.value};
    if (eventBridgeSubscriptions_.contains(key)) {
        // Defensive idempotency only — Created is documented to fire
        // exactly once per behaviour instance, so this should never
        // trigger, but subscribing a second time would silently
        // double-deliver every bus event to this graph.
        return;
    }
    std::vector<EventSubscriptionHandle> handles;
    kb::scene::Scene& scene = context.GetScene();
    for (const kb::visual::VisualGraphIrFunction& function : artifact->module.functions) {
        if (function.customEventName.empty()) {
            continue;
        }
        const EventSubscriptionHandle handle = eventBus->Subscribe(
            function.customEventName,
            [this, &scene, eventBus, entity, assetId](const ScriptEvent& event) {
                // Re-resolve the LIVE BehaviourComponent (not a captured
                // snapshot) so a since-disabled behaviour is honestly
                // skipped by ExecuteBehaviour's own `!behaviour.enabled`
                // check, exactly like a normal DispatchEvent-driven call
                // would see.
                const kb::scene::BehaviourComponent* liveBehaviour = scene.Components().Behaviours().TryGet(entity);
                if (liveBehaviour == nullptr) {
                    return;
                }
                kb::visual::VisualGraphBehaviourInstance* instance = instances_.Find(entity, assetId);
                if (instance == nullptr) {
                    return;
                }
                // "self" is stored by ExecuteBehaviour itself (inside
                // ExecuteCustomEvent's call chain); "deltaSeconds" is not —
                // 0.0F is the honest value for an out-of-band bus event,
                // which is not tied to any particular frame's delta the
                // way a normal Tick/lifecycle dispatch is.
                instance->context.Store(0U, "deltaSeconds", kb::visual::VisualGraphRuntimeValue{0.0F});
                const std::vector<kb::visual::VisualGraphCustomEventArgument> arguments = ToVisualGraphArguments(event);
                static_cast<void>(kb::visual::VisualGraphBehaviourRuntime::ExecuteCustomEvent(
                    *liveBehaviour, entity, event.name, arguments, artifacts_, bindings_, instance->context));
                // A bus-triggered graph run can itself EmitEvent — drain
                // those onward through the bus too, the same as any other
                // execution path (AppendEmittedEvents above); there is no
                // ScriptExecutionContext here to also feed the OLD
                // DispatchEvent mechanism, so this is bus-only for events
                // emitted FROM a bus-triggered run — an honest, documented
                // asymmetry, not a silent drop.
                if (eventBus != nullptr) {
                    BroadcastEmittedEventsToBus(instance->context, scene, *eventBus);
                }
            },
            entity);
        if (handle != kInvalidEventSubscriptionHandle) {
            handles.push_back(handle);
        }
    }
    if (!handles.empty()) {
        eventBridgeSubscriptions_.emplace(key, std::move(handles));
    }
}

void VisualGraphScriptBackend::UnsubscribeCustomEventsFromBus(kb::assets::AssetId assetId, kb::scene::SceneEntity entity, ScriptEventBus* eventBus) {
    const EventBridgeKey key{entity.Id(), assetId.value};
    const auto iterator = eventBridgeSubscriptions_.find(key);
    if (iterator == eventBridgeSubscriptions_.end()) {
        return;
    }
    if (eventBus != nullptr) {
        for (const EventSubscriptionHandle handle : iterator->second) {
            static_cast<void>(eventBus->Unsubscribe(handle));
        }
    }
    eventBridgeSubscriptions_.erase(iterator);
}

std::vector<kb::visual::VisualGraphCustomEventArgument> VisualGraphScriptBackend::ToVisualGraphArguments(const ScriptEvent& event) {
    std::vector<kb::visual::VisualGraphCustomEventArgument> arguments;
    arguments.reserve(event.arguments.size());
    for (const ScriptEventArgument& argument : event.arguments) {
        arguments.push_back(kb::visual::VisualGraphCustomEventArgument{
            .name = argument.name,
            .value = argument.value.ToVisualGraphValue(),
        });
    }
    return arguments;
}

ScriptBackendExecutionResult VisualGraphScriptBackend::ToScriptResult(
    const kb::visual::VisualGraphBehaviourExecutionResult& result,
    const kb::scene::BehaviourComponent& behaviour,
    kb::scene::SceneEntity entity) {
    ScriptBackendExecutionResult scriptResult{
        .executed = result.runtime.executed,
    };
    scriptResult.diagnostics.reserve(result.diagnostics.size());
    for (const kb::visual::VisualGraphDiagnostic& diagnostic : result.diagnostics) {
        scriptResult.diagnostics.push_back(ScriptDiagnostic{
            .entity = entity,
            .assetId = kb::assets::AssetId{behaviour.behaviourAssetId},
            .backend = behaviour.backend,
            .message = diagnostic.message,
        });
    }
    return scriptResult;
}

} // namespace kb::script
