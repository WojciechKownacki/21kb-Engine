#pragma once

#include "engine/script/ScriptBackend.hpp"
#include "engine/visual/VisualGraphBehaviourInstanceRegistry.hpp"
#include "engine/visual/VisualGraphBehaviourRuntime.hpp"
#include "engine/visual/VisualGraphRuntimeBindingRegistry.hpp"
#include "engine/visual/VisualGraphRuntimeExecutor.hpp"
#include "engine/visual/VisualGraphRuntimeRegistry.hpp"

#include <vector>

namespace kb::script {

class VisualGraphScriptBackend final : public IScriptBackend {
public:
    VisualGraphScriptBackend(
        const kb::visual::VisualGraphRuntimeRegistry& artifacts,
        const kb::visual::VisualGraphRuntimeBindingRegistry& bindings,
        kb::visual::VisualGraphBehaviourInstanceRegistry& instances) noexcept;

    [[nodiscard]] kb::scene::BehaviourBackend Backend() const noexcept override;
    [[nodiscard]] ScriptBackendExecutionResult ExecuteLifecycle(const kb::scene::BehaviourComponent& behaviour, ScriptExecutionContext& context) override;
    [[nodiscard]] ScriptBackendExecutionResult ExecuteEvent(const kb::scene::BehaviourComponent& behaviour, const ScriptEvent& event, EventId eventId, ScriptExecutionContext& context) override;

private:
    [[nodiscard]] kb::visual::VisualGraphRuntimeExecutionContext& ContextFor(const kb::scene::BehaviourComponent& behaviour, kb::scene::SceneEntity entity);
    void StoreCommonInputs(kb::visual::VisualGraphRuntimeExecutionContext& context, const ScriptExecutionContext& scriptContext) const;
    void AppendEmittedEvents(kb::visual::VisualGraphRuntimeExecutionContext& graphContext, ScriptExecutionContext& scriptContext, kb::scene::SceneEntity sender, kb::assets::AssetId assetId) const;
    [[nodiscard]] static std::vector<kb::visual::VisualGraphCustomEventArgument> ToVisualGraphArguments(const ScriptEvent& event);
    [[nodiscard]] static ScriptBackendExecutionResult ToScriptResult(
        const kb::visual::VisualGraphBehaviourExecutionResult& result,
        const kb::scene::BehaviourComponent& behaviour,
        kb::scene::SceneEntity entity);

    const kb::visual::VisualGraphRuntimeRegistry& artifacts_;
    const kb::visual::VisualGraphRuntimeBindingRegistry& bindings_;
    kb::visual::VisualGraphBehaviourInstanceRegistry& instances_;
};

} // namespace kb::script
