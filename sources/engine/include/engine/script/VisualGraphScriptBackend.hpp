#pragma once

#include "engine/script/ScriptBackend.hpp"
#include "engine/script/ScriptExecutionBudget.hpp"
#include "engine/visual/VisualGraphBehaviourInstanceRegistry.hpp"
#include "engine/visual/VisualGraphBehaviourRuntime.hpp"
#include "engine/visual/VisualGraphRuntimeBindingRegistry.hpp"
#include "engine/visual/VisualGraphRuntimeExecutor.hpp"
#include "engine/visual/VisualGraphRuntimeRegistry.hpp"
#include "engine/visual/VisualGraphDebugSession.hpp"

#include <cstdint>
#include <map>
#include <vector>

namespace kb::script {

class VisualGraphScriptBackend final : public IScriptBackend {
public:
    VisualGraphScriptBackend(
        const kb::visual::VisualGraphRuntimeRegistry& artifacts,
        const kb::visual::VisualGraphRuntimeBindingRegistry& bindings,
        kb::visual::VisualGraphBehaviourInstanceRegistry& instances,
        kb::visual::VisualGraphDebugSession* debugger = nullptr,
        ScriptExecutionBudgetSettings executionBudgetSettings = {}) noexcept;

    [[nodiscard]] kb::scene::BehaviourBackend Backend() const noexcept override;
    [[nodiscard]] ScriptBackendExecutionResult ExecuteLifecycle(const kb::scene::BehaviourComponent& behaviour, ScriptExecutionContext& context) override;
    [[nodiscard]] ScriptBackendExecutionResult ExecuteEvent(const kb::scene::BehaviourComponent& behaviour, const ScriptEvent& event, EventId eventId, ScriptExecutionContext& context) override;
    void ResetAssetForHotReload(kb::assets::AssetId assetId, ScriptEventBus& events) noexcept override;

private:
    [[nodiscard]] kb::visual::VisualGraphRuntimeExecutionContext& ContextFor(const kb::scene::BehaviourComponent& behaviour, kb::scene::SceneEntity entity);
    void StoreCommonInputs(kb::visual::VisualGraphRuntimeExecutionContext& context, const ScriptExecutionContext& scriptContext) const;
    void AppendEmittedEvents(kb::visual::VisualGraphRuntimeExecutionContext& graphContext, ScriptExecutionContext& scriptContext, kb::scene::SceneEntity sender, kb::assets::AssetId assetId) const;
    [[nodiscard]] static std::vector<kb::visual::VisualGraphCustomEventArgument> ToVisualGraphArguments(const ScriptEvent& event);
    [[nodiscard]] static ScriptBackendExecutionResult ToScriptResult(
        const kb::visual::VisualGraphBehaviourExecutionResult& result,
        const kb::scene::BehaviourComponent& behaviour,
        kb::scene::SceneEntity entity);

    // LIB-112: gameplay event bridge to ScriptEventBus (LIB-105). Graph
    // emissions retain the pre-existing behaviour dispatch and also reach
    // regular bus consumers; ScriptEventBusAudience prevents the bridge
    // subscription from consuming that mirrored copy a second time.
    void BroadcastEmittedEventsToBus(
        kb::visual::VisualGraphRuntimeExecutionContext& graphContext,
        kb::scene::Scene& scene,
        ScriptEventBus& eventBus,
        kb::scene::SceneEntity sender,
        kb::assets::AssetId senderAsset,
        bool mirroredToBehaviourDispatcher) const;
    void SubscribeCustomEventsToBus(const kb::scene::BehaviourComponent& behaviour, ScriptExecutionContext& context);
    void UnsubscribeCustomEventsFromBus(kb::assets::AssetId assetId, kb::scene::SceneEntity entity, ScriptEventBus* eventBus);

    struct EventBridgeKey {
        std::uint64_t entityId = 0;
        std::uint64_t assetId = 0;

        [[nodiscard]] friend bool operator<(const EventBridgeKey& lhs, const EventBridgeKey& rhs) noexcept {
            return lhs.entityId != rhs.entityId ? lhs.entityId < rhs.entityId : lhs.assetId < rhs.assetId;
        }
    };

    const kb::visual::VisualGraphRuntimeRegistry& artifacts_;
    const kb::visual::VisualGraphRuntimeBindingRegistry& bindings_;
    kb::visual::VisualGraphBehaviourInstanceRegistry& instances_;
    kb::visual::VisualGraphDebugSession* debugger_ = nullptr;
    ScriptExecutionBudgetSettings executionBudgetSettings_;
    // LIB-112: one entry per (entity, asset) behaviour instance that has
    // ANY CustomEvent node — populated on Created, erased on Destroyed.
    // std::map (not unordered_map) because this is Created/Destroyed-rate
    // traffic, not per-frame — a hasher isn't worth writing for it.
    std::map<EventBridgeKey, std::vector<EventSubscriptionHandle>> eventBridgeSubscriptions_;
};

} // namespace kb::script
