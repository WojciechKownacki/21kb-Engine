#include "engine/script/VisualGraphScriptBackend.hpp"

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
    static_cast<void>(sender);
    static_cast<void>(assetId);
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
