#include "engine/script/ScriptRuntimeSceneSystem.hpp"

#include "engine/scene/SceneSystemContext.hpp"

#include <utility>

namespace kb::script {
namespace {

void MergeResult(ScriptRuntimeExecutionResult& target, ScriptRuntimeExecutionResult source) {
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

} // namespace

ScriptRuntimeSceneSystem::ScriptRuntimeSceneSystem(ScriptRuntime& runtime) noexcept
    : runtime_(runtime) {}

void ScriptRuntimeSceneSystem::OnCreate(kb::scene::SceneSystemContext& context) {
    lastResult_ = {};
    MergeResult(lastResult_, runtime_.ExecuteLifecycleAndDispatchEvents(context.GetScene(), ScriptLifecycleEvent::Created, context.DeltaSeconds()));
    MergeResult(lastResult_, runtime_.ExecuteLifecycleAndDispatchEvents(context.GetScene(), ScriptLifecycleEvent::Activated, context.DeltaSeconds()));
    MergeResult(lastResult_, runtime_.ExecuteLifecycleAndDispatchEvents(context.GetScene(), ScriptLifecycleEvent::Ready, context.DeltaSeconds()));
}

void ScriptRuntimeSceneSystem::OnUpdate(kb::scene::SceneSystemContext& context) {
    lastResult_ = runtime_.ExecuteLifecycleAndDispatchEvents(context.GetScene(), ScriptLifecycleEvent::Tick, context.DeltaSeconds());
}

void ScriptRuntimeSceneSystem::OnDestroy(kb::scene::SceneSystemContext& context) {
    lastResult_ = {};
    MergeResult(lastResult_, runtime_.ExecuteLifecycleAndDispatchEvents(context.GetScene(), ScriptLifecycleEvent::Deactivated, context.DeltaSeconds()));
    MergeResult(lastResult_, runtime_.ExecuteLifecycleAndDispatchEvents(context.GetScene(), ScriptLifecycleEvent::Destroyed, context.DeltaSeconds()));
}

const ScriptRuntimeExecutionResult& ScriptRuntimeSceneSystem::LastResult() const noexcept {
    return lastResult_;
}

} // namespace kb::script
