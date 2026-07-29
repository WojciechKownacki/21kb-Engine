#include "engine/visual/VisualGraphBehaviourRuntime.hpp"

namespace kb::visual {
namespace {

[[nodiscard]] VisualGraphBehaviourExecutionResult ExecuteBehaviour(
    const kb::scene::BehaviourComponent& behaviour,
    kb::scene::SceneEntity entity,
    const VisualGraphRuntimeRegistry& artifacts,
    const VisualGraphRuntimeBindingRegistry& bindings,
    VisualGraphRuntimeExecutionContext& context,
    VisualGraphDebugSession* debugger,
    auto execute) {
    VisualGraphBehaviourExecutionResult result{};
    if (!behaviour.enabled || behaviour.backend != kb::scene::BehaviourBackend::VisualGraph) {
        return result;
    }

    const kb::assets::AssetId assetId{behaviour.behaviourAssetId};
    if (!assetId.IsValid()) {
        result.diagnostics.push_back(VisualGraphDiagnostics::Error(VisualGraphDiagnosticStage::Runtime, "visual graph behaviour has no valid asset id"));
        return result;
    }

    const VisualGraphRuntimeArtifact* artifact = artifacts.Find(assetId);
    if (artifact == nullptr) {
        result.diagnostics.push_back(VisualGraphDiagnostics::Error(VisualGraphDiagnosticStage::Runtime, "visual graph runtime artifact is missing"));
        return result;
    }

    context.Store(0U, "self", VisualGraphRuntimeValue{entity.Id(), VisualGraphValueType::Entity});
    const VisualGraphRuntimeExecutor executor{bindings, debugger};
    result.runtime = execute(executor, *artifact, context);
    result.diagnostics = result.runtime.diagnostics.empty() ? VisualGraphDiagnostics::FromErrors(VisualGraphDiagnosticStage::Runtime, result.runtime.errors)
                                                            : result.runtime.diagnostics;
    return result;
}

} // namespace

VisualGraphBehaviourExecutionResult VisualGraphBehaviourRuntime::Execute(
    const kb::scene::BehaviourComponent& behaviour,
    kb::scene::SceneEntity entity,
    VisualGraphLifecycleEvent event,
    const VisualGraphRuntimeRegistry& artifacts,
    const VisualGraphRuntimeBindingRegistry& bindings,
    VisualGraphRuntimeExecutionContext& context,
    VisualGraphDebugSession* debugger) {
    return ExecuteBehaviour(behaviour, entity, artifacts, bindings, context, debugger, [event](const VisualGraphRuntimeExecutor& executor, const VisualGraphRuntimeArtifact& artifact, VisualGraphRuntimeExecutionContext& runtimeContext) {
        return executor.Execute(artifact, event, runtimeContext);
    });
}

VisualGraphBehaviourExecutionResult VisualGraphBehaviourRuntime::ExecuteCustomEvent(
    const kb::scene::BehaviourComponent& behaviour,
    kb::scene::SceneEntity entity,
    std::string_view eventName,
    const VisualGraphRuntimeRegistry& artifacts,
    const VisualGraphRuntimeBindingRegistry& bindings,
    VisualGraphRuntimeExecutionContext& context,
    VisualGraphDebugSession* debugger) {
    return ExecuteCustomEvent(behaviour, entity, eventName, std::span<const VisualGraphCustomEventArgument>{}, artifacts, bindings, context, debugger);
}

VisualGraphBehaviourExecutionResult VisualGraphBehaviourRuntime::ExecuteCustomEvent(
    const kb::scene::BehaviourComponent& behaviour,
    kb::scene::SceneEntity entity,
    std::string_view eventName,
    std::span<const VisualGraphCustomEventArgument> arguments,
    const VisualGraphRuntimeRegistry& artifacts,
    const VisualGraphRuntimeBindingRegistry& bindings,
    VisualGraphRuntimeExecutionContext& context,
    VisualGraphDebugSession* debugger) {
    return ExecuteBehaviour(behaviour, entity, artifacts, bindings, context, debugger, [eventName, arguments](const VisualGraphRuntimeExecutor& executor, const VisualGraphRuntimeArtifact& artifact, VisualGraphRuntimeExecutionContext& runtimeContext) {
        return executor.ExecuteCustomEvent(artifact, eventName, arguments, runtimeContext);
    });
}

} // namespace kb::visual
