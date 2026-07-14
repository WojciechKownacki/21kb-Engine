#include "engine/script/ScriptRuntime.hpp"

#include "engine/scene/BehaviourExecutionOrder.hpp"
#include "engine/scene/SceneBehaviourComponents.hpp"
#include "engine/scene/SceneComponents.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace kb::script {
namespace {

struct BehaviourDispatchRecord {
    kb::scene::SceneEntity entity{};
    kb::scene::BehaviourComponent behaviour{};
};

struct DispatchContext {
    kb::scene::Scene* scene = nullptr;
    ScriptRuntime* runtime = nullptr;
    ScriptSharedState* sharedState = nullptr;
    ScriptFunctionRegistry* functions = nullptr;
    ScriptLifecycleEvent lifecycle = ScriptLifecycleEvent::Tick;
    const ScriptEvent* event = nullptr;
    // LIB-104: computed ONCE per dispatch (DispatchEvent), not once per
    // behaviour — see ScriptEventId.hpp/IScriptBackend::ExecuteEvent.
    EventId eventId = 0;
    float deltaSeconds = 0.0F;
    ScriptRuntimeExecutionResult* result = nullptr;
};

[[nodiscard]] bool ComesBefore(const BehaviourDispatchRecord& lhs, const BehaviourDispatchRecord& rhs) noexcept {
    return kb::scene::BehaviourExecutionOrderLess(lhs.entity, lhs.behaviour, rhs.entity, rhs.behaviour);
}

void CollectBehaviour(kb::scene::SceneEntity entity, const kb::scene::BehaviourComponent& behaviour, void* rawContext) {
    auto& records = *static_cast<std::vector<BehaviourDispatchRecord>*>(rawContext);
    records.push_back(BehaviourDispatchRecord{
        .entity = entity,
        .behaviour = behaviour,
    });
}

void AppendDiagnostics(std::vector<ScriptDiagnostic>& target, std::vector<ScriptDiagnostic> source) {
    target.reserve(target.size() + source.size());
    for (ScriptDiagnostic& diagnostic : source) {
        target.push_back(std::move(diagnostic));
    }
}

void AppendEvents(std::vector<ScriptEvent>& target, std::vector<ScriptEvent> source) {
    target.reserve(target.size() + source.size());
    for (ScriptEvent& event : source) {
        target.push_back(std::move(event));
    }
}

void AddMissingBackendDiagnostic(DispatchContext& dispatch, kb::scene::SceneEntity entity, const kb::scene::BehaviourComponent& behaviour) {
    dispatch.result->diagnostics.push_back(ScriptDiagnostic{
        .entity = entity,
        .assetId = kb::assets::AssetId{behaviour.behaviourAssetId},
        .backend = behaviour.backend,
        .message = "script backend is not registered",
    });
}

[[nodiscard]] bool ShouldSkipDisabled(const kb::scene::BehaviourComponent& behaviour, const DispatchContext& dispatch) noexcept {
    return !behaviour.enabled && dispatch.lifecycle != ScriptLifecycleEvent::Deactivated && dispatch.lifecycle != ScriptLifecycleEvent::Destroyed;
}

void DispatchBehaviour(kb::scene::SceneEntity entity, const kb::scene::BehaviourComponent& behaviour, DispatchContext& dispatch) {
    ++dispatch.result->visitedBehaviours;
    if (ShouldSkipDisabled(behaviour, dispatch)) {
        return;
    }

    IScriptBackend* backend = dispatch.runtime->FindBackend(behaviour.backend);
    if (backend == nullptr) {
        AddMissingBackendDiagnostic(dispatch, entity, behaviour);
        return;
    }

    std::vector<ScriptEvent> emittedEvents;
    ScriptExecutionContext scriptContext{
        *dispatch.scene,
        entity,
        kb::assets::AssetId{behaviour.behaviourAssetId},
        behaviour.backend,
        dispatch.lifecycle,
        dispatch.deltaSeconds,
        &emittedEvents,
        dispatch.event,
        dispatch.sharedState,
        dispatch.functions,
    };

    const ScriptBackendExecutionResult backendResult = dispatch.event == nullptr
        ? backend->ExecuteLifecycle(behaviour, scriptContext)
        : backend->ExecuteEvent(behaviour, *dispatch.event, dispatch.eventId, scriptContext);
    if (backendResult.executed && backendResult.Succeeded()) {
        ++dispatch.result->executedBehaviours;
    }
    AppendEvents(dispatch.result->emittedEvents, std::move(emittedEvents));
    AppendDiagnostics(dispatch.result->diagnostics, backendResult.diagnostics);
}

void DispatchSceneBehaviours(kb::scene::Scene& scene, DispatchContext& context) {
    std::vector<BehaviourDispatchRecord> behaviours;
    scene.Components().Behaviours().ForEach(&CollectBehaviour, &behaviours);
    std::ranges::sort(behaviours, &ComesBefore);
    for (const BehaviourDispatchRecord& record : behaviours) {
        if (context.event != nullptr && context.event->target.IsValid() && record.entity != context.event->target) {
            continue;
        }
        DispatchBehaviour(record.entity, record.behaviour, context);
    }
}

} // namespace

bool ScriptRuntime::RegisterBackend(std::unique_ptr<IScriptBackend> backend) {
    if (backend == nullptr) {
        return false;
    }
    const std::size_t index = BackendIndex(backend->Backend());
    if (index >= backends_.size()) {
        return false;
    }
    backends_[index] = std::move(backend);
    return true;
}

IScriptBackend* ScriptRuntime::FindBackend(kb::scene::BehaviourBackend backend) noexcept {
    const std::size_t index = BackendIndex(backend);
    return index >= backends_.size() ? nullptr : backends_[index].get();
}

const IScriptBackend* ScriptRuntime::FindBackend(kb::scene::BehaviourBackend backend) const noexcept {
    const std::size_t index = BackendIndex(backend);
    return index >= backends_.size() ? nullptr : backends_[index].get();
}

ScriptSharedState& ScriptRuntime::SharedState() noexcept {
    return sharedState_;
}

const ScriptSharedState& ScriptRuntime::SharedState() const noexcept {
    return sharedState_;
}

ScriptFunctionRegistry& ScriptRuntime::Functions() noexcept {
    return functions_;
}

const ScriptFunctionRegistry& ScriptRuntime::Functions() const noexcept {
    return functions_;
}

ScriptRuntimeExecutionResult ScriptRuntime::ExecuteLifecycle(kb::scene::Scene& scene, ScriptLifecycleEvent event, float deltaSeconds) {
    // LIB-021: the first lifecycle dispatch locks the function registry —
    // once the world is running, a new function could be visible to some
    // already-running dispatch paths (Lua sugar, compiled Visual Graph
    // bindings) but not others.
    functions_.Lock();
    ScriptRuntimeExecutionResult result{};
    DispatchContext context{
        .scene = &scene,
        .runtime = this,
        .sharedState = &sharedState_,
        .functions = &functions_,
        .lifecycle = event,
        .deltaSeconds = deltaSeconds,
        .result = &result,
    };
    DispatchSceneBehaviours(scene, context);
    return result;
}

ScriptRuntimeExecutionResult ScriptRuntime::ExecuteLifecycleForBehaviour(
    kb::scene::Scene& scene,
    kb::scene::SceneEntity entity,
    const kb::scene::BehaviourComponent& behaviour,
    ScriptLifecycleEvent event,
    float deltaSeconds) {
    functions_.Lock();
    ScriptRuntimeExecutionResult result{};
    DispatchContext context{
        .scene = &scene,
        .runtime = this,
        .sharedState = &sharedState_,
        .functions = &functions_,
        .lifecycle = event,
        .deltaSeconds = deltaSeconds,
        .result = &result,
    };
    DispatchBehaviour(entity, behaviour, context);
    return result;
}

ScriptRuntimeExecutionResult ScriptRuntime::ExecuteLifecycleForBehaviourAndDispatchEvents(
    kb::scene::Scene& scene,
    kb::scene::SceneEntity entity,
    const kb::scene::BehaviourComponent& behaviour,
    ScriptLifecycleEvent event,
    float deltaSeconds,
    ScriptRuntimeDispatchOptions options) {
    ScriptRuntimeExecutionResult result = ExecuteLifecycleForBehaviour(scene, entity, behaviour, event, deltaSeconds);
    const std::vector<ScriptEvent> emittedEvents = result.emittedEvents;
    MergeResult(result, DrainEvents(scene, emittedEvents, deltaSeconds, options));
    return result;
}

ScriptRuntimeExecutionResult ScriptRuntime::DispatchEvent(kb::scene::Scene& scene, const ScriptEvent& event, float deltaSeconds) {
    functions_.Lock();
    ScriptRuntimeExecutionResult result{};
    if (event.name.empty()) {
        result.diagnostics.push_back(ScriptDiagnostic{
            .message = "script event name is empty",
        });
        return result;
    }
    DispatchContext context{
        .scene = &scene,
        .runtime = this,
        .sharedState = &sharedState_,
        .functions = &functions_,
        .lifecycle = ScriptLifecycleEvent::Tick,
        .event = &event,
        .eventId = event.Id(),
        .deltaSeconds = deltaSeconds,
        .result = &result,
    };
    DispatchSceneBehaviours(scene, context);
    return result;
}

ScriptRuntimeExecutionResult ScriptRuntime::ExecuteLifecycleAndDispatchEvents(
    kb::scene::Scene& scene,
    ScriptLifecycleEvent event,
    float deltaSeconds,
    ScriptRuntimeDispatchOptions options) {
    ScriptRuntimeExecutionResult result = ExecuteLifecycle(scene, event, deltaSeconds);
    const std::vector<ScriptEvent> emittedEvents = result.emittedEvents;
    MergeResult(result, DrainEvents(scene, emittedEvents, deltaSeconds, options));
    return result;
}

ScriptRuntimeExecutionResult ScriptRuntime::DispatchEventAndDrain(
    kb::scene::Scene& scene,
    const ScriptEvent& event,
    float deltaSeconds,
    ScriptRuntimeDispatchOptions options) {
    ScriptRuntimeExecutionResult result = DispatchEvent(scene, event, deltaSeconds);
    const std::vector<ScriptEvent> emittedEvents = result.emittedEvents;
    MergeResult(result, DrainEvents(scene, emittedEvents, deltaSeconds, options));
    return result;
}

std::size_t ScriptRuntime::BackendIndex(kb::scene::BehaviourBackend backend) noexcept {
    return static_cast<std::size_t>(backend);
}

void ScriptRuntime::MergeResult(ScriptRuntimeExecutionResult& target, ScriptRuntimeExecutionResult source) {
    target.visitedBehaviours += source.visitedBehaviours;
    target.executedBehaviours += source.executedBehaviours;
    target.emittedEvents.reserve(target.emittedEvents.size() + source.emittedEvents.size());
    for (ScriptEvent& event : source.emittedEvents) {
        target.emittedEvents.push_back(std::move(event));
    }
    target.diagnostics.reserve(target.diagnostics.size() + source.diagnostics.size());
    for (ScriptDiagnostic& diagnostic : source.diagnostics) {
        target.diagnostics.push_back(std::move(diagnostic));
    }
}

ScriptRuntimeExecutionResult ScriptRuntime::DrainEvents(
    kb::scene::Scene& scene,
    std::vector<ScriptEvent> initialEvents,
    float deltaSeconds,
    ScriptRuntimeDispatchOptions options) {
    ScriptRuntimeExecutionResult result{};
    std::vector<ScriptEvent> pending = std::move(initialEvents);
    for (std::size_t depth = 0; depth < options.maxEventDepth && !pending.empty(); ++depth) {
        std::vector<ScriptEvent> next;
        for (const ScriptEvent& event : pending) {
            ScriptRuntimeExecutionResult dispatched = DispatchEvent(scene, event, deltaSeconds);
            next.reserve(next.size() + dispatched.emittedEvents.size());
            for (ScriptEvent& emitted : dispatched.emittedEvents) {
                next.push_back(std::move(emitted));
            }
            dispatched.emittedEvents.clear();
            MergeResult(result, std::move(dispatched));
        }
        pending = std::move(next);
    }
    if (!pending.empty()) {
        result.diagnostics.push_back(ScriptDiagnostic{
            .message = "script event dispatch depth limit reached",
        });
    }
    return result;
}

} // namespace kb::script
