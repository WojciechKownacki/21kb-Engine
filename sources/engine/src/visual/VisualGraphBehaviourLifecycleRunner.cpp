#include "engine/visual/VisualGraphBehaviourLifecycleRunner.hpp"

#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/SceneBehaviourComponents.hpp"
#include "engine/scene/SceneComponents.hpp"

#include <utility>

namespace kb::visual {

namespace {

struct BehaviourLifecycleVisitorContext {
    VisualGraphLifecycleEvent event = VisualGraphLifecycleEvent::Tick;
    std::string_view customEventName;
    std::span<const VisualGraphCustomEventArgument> customEventArguments;
    bool executeCustomEvent = false;
    const VisualGraphRuntimeRegistry* artifacts = nullptr;
    const VisualGraphRuntimeBindingRegistry* bindings = nullptr;
    VisualGraphBehaviourInstanceRegistry* instances = nullptr;
    VisualGraphBehaviourLifecycleResult* result = nullptr;
};

void AppendDiagnostics(std::vector<VisualGraphDiagnostic>& target, std::vector<VisualGraphDiagnostic> source) {
    target.reserve(target.size() + source.size());
    for (VisualGraphDiagnostic& diagnostic : source) {
        target.push_back(std::move(diagnostic));
    }
}

void AppendEmittedEvents(
    std::vector<VisualGraphBehaviourEmittedEvent>& target,
    kb::scene::SceneEntity sender,
    kb::assets::AssetId assetId,
    const std::vector<VisualGraphEmittedEvent>& source) {
    target.reserve(target.size() + source.size());
    for (const VisualGraphEmittedEvent& event : source) {
        target.push_back(VisualGraphBehaviourEmittedEvent{
            .sender = sender,
            .target = event.target,
            .assetId = assetId,
            .name = event.name,
            .arguments = event.arguments,
        });
    }
}

void ExecuteVisualGraphBehaviour(kb::scene::SceneEntity entity, const kb::scene::BehaviourComponent& behaviour, void* rawContext) {
    auto& context = *static_cast<BehaviourLifecycleVisitorContext*>(rawContext);
    ++context.result->visitedBehaviours;

    if (!behaviour.enabled || behaviour.backend != kb::scene::BehaviourBackend::VisualGraph) {
        return;
    }

    const kb::assets::AssetId assetId{behaviour.behaviourAssetId};
    VisualGraphRuntimeExecutionContext scratchContext;
    VisualGraphRuntimeExecutionContext& executionContext = assetId.IsValid() ? context.instances->FindOrCreate(entity, assetId).context : scratchContext;
    const VisualGraphBehaviourExecutionResult executed = context.executeCustomEvent
        ? VisualGraphBehaviourRuntime::ExecuteCustomEvent(behaviour, entity, context.customEventName, context.customEventArguments, *context.artifacts, *context.bindings, executionContext)
        : VisualGraphBehaviourRuntime::Execute(behaviour, entity, context.event, *context.artifacts, *context.bindings, executionContext);
    if (executed.runtime.executed && executed.Succeeded()) {
        ++context.result->executedBehaviours;
    }
    AppendEmittedEvents(context.result->emittedEvents, entity, assetId, executionContext.EmittedEventRecords());
    AppendDiagnostics(context.result->diagnostics, executed.diagnostics);
}

} // namespace

VisualGraphBehaviourLifecycleResult VisualGraphBehaviourLifecycleRunner::Execute(
    kb::scene::Scene& scene,
    VisualGraphLifecycleEvent event,
    const VisualGraphRuntimeRegistry& artifacts,
    const VisualGraphRuntimeBindingRegistry& bindings,
    VisualGraphBehaviourInstanceRegistry& instances) {
    VisualGraphBehaviourLifecycleResult result{};
    BehaviourLifecycleVisitorContext context{
        .event = event,
        .artifacts = &artifacts,
        .bindings = &bindings,
        .instances = &instances,
        .result = &result,
    };
    scene.Components().Behaviours().ForEach(&ExecuteVisualGraphBehaviour, &context);
    return result;
}

VisualGraphBehaviourLifecycleResult VisualGraphBehaviourLifecycleRunner::ExecuteCustomEvent(
    kb::scene::Scene& scene,
    std::string_view eventName,
    const VisualGraphRuntimeRegistry& artifacts,
    const VisualGraphRuntimeBindingRegistry& bindings,
    VisualGraphBehaviourInstanceRegistry& instances) {
    return ExecuteCustomEvent(scene, eventName, std::span<const VisualGraphCustomEventArgument>{}, artifacts, bindings, instances);
}

VisualGraphBehaviourLifecycleResult VisualGraphBehaviourLifecycleRunner::ExecuteCustomEvent(
    kb::scene::Scene& scene,
    std::string_view eventName,
    std::span<const VisualGraphCustomEventArgument> arguments,
    const VisualGraphRuntimeRegistry& artifacts,
    const VisualGraphRuntimeBindingRegistry& bindings,
    VisualGraphBehaviourInstanceRegistry& instances) {
    VisualGraphBehaviourLifecycleResult result{};
    if (eventName.empty()) {
        result.diagnostics.push_back(VisualGraphDiagnostics::Error(VisualGraphDiagnosticStage::Runtime, "visual graph custom event name is empty"));
        return result;
    }

    BehaviourLifecycleVisitorContext context{
        .customEventName = eventName,
        .customEventArguments = arguments,
        .executeCustomEvent = true,
        .artifacts = &artifacts,
        .bindings = &bindings,
        .instances = &instances,
        .result = &result,
    };
    scene.Components().Behaviours().ForEach(&ExecuteVisualGraphBehaviour, &context);
    return result;
}

} // namespace kb::visual
