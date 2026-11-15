#pragma once

#include "engine/scene/Scene.hpp"
#include "engine/script/ScriptBackend.hpp"
#include "engine/script/ScriptEvent.hpp"
#include "engine/script/ScriptEventBus.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptLifecycle.hpp"
#include "engine/script/ScriptSharedState.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kb::script {

struct ScriptRuntimeDispatchOptions {
    std::size_t maxEventDepth = 8;
};

struct ScriptRuntimeExecutionResult {
    std::size_t visitedBehaviours = 0;
    std::size_t executedBehaviours = 0;
    std::vector<ScriptEvent> emittedEvents;
    std::vector<ScriptDiagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept {
        return diagnostics.empty();
    }
};

class ScriptRuntime final {
public:
    ScriptRuntime() = default;

    [[nodiscard]] bool RegisterBackend(std::unique_ptr<IScriptBackend> backend);
    [[nodiscard]] IScriptBackend* FindBackend(kb::scene::BehaviourBackend backend) noexcept;
    [[nodiscard]] const IScriptBackend* FindBackend(kb::scene::BehaviourBackend backend) const noexcept;
    [[nodiscard]] ScriptSharedState& SharedState() noexcept;
    [[nodiscard]] const ScriptSharedState& SharedState() const noexcept;
    [[nodiscard]] ScriptFunctionRegistry& Functions() noexcept;
    [[nodiscard]] const ScriptFunctionRegistry& Functions() const noexcept;
    // LIB-105: the pub/sub bus every Events.Subscribe/Unsubscribe/Emit/
    // EmitDeferred/Broadcast call site (native via ScriptExecutionContext::
    // Events(), Lua via the bespoke `Events` table) ultimately reaches —
    // owned here rather than per-Scene because ScriptRuntimeHost binds
    // exactly one Scene at construction (confirmed by reading its
    // constructor), making runtime-scoped and scene-scoped equivalent for
    // every real call site in this codebase today.
    [[nodiscard]] ScriptEventBus& Events() noexcept;
    [[nodiscard]] const ScriptEventBus& Events() const noexcept;

    [[nodiscard]] ScriptRuntimeExecutionResult ExecuteLifecycle(kb::scene::Scene& scene, ScriptLifecycleEvent event, float deltaSeconds);
    [[nodiscard]] ScriptRuntimeExecutionResult ExecuteLifecycleForBehaviour(
        kb::scene::Scene& scene,
        kb::scene::SceneEntity entity,
        const kb::scene::BehaviourComponent& behaviour,
        ScriptLifecycleEvent event,
        float deltaSeconds);
    [[nodiscard]] ScriptRuntimeExecutionResult ExecuteLifecycleForBehaviourAndDispatchEvents(
        kb::scene::Scene& scene,
        kb::scene::SceneEntity entity,
        const kb::scene::BehaviourComponent& behaviour,
        ScriptLifecycleEvent event,
        float deltaSeconds,
        ScriptRuntimeDispatchOptions options = {});
    [[nodiscard]] ScriptRuntimeExecutionResult DispatchEvent(kb::scene::Scene& scene, const ScriptEvent& event, float deltaSeconds);
    [[nodiscard]] ScriptRuntimeExecutionResult ExecuteLifecycleAndDispatchEvents(
        kb::scene::Scene& scene,
        ScriptLifecycleEvent event,
        float deltaSeconds,
        ScriptRuntimeDispatchOptions options = {});
    // LIB-107: "AndDrain" here means something DIFFERENT from ScriptEventBus
    // ::DrainDeferred, despite the shared word — this drains
    // ScriptExecutionContext::Emit/EmitTo's own emittedEvents queue
    // SYNCHRONOUSLY, recursively, within THIS SAME call (bounded by
    // ScriptRuntimeDispatchOptions::maxEventDepth, LIB-038's reentrancy
    // discipline) — every event this dispatch chain produces is fully
    // delivered before this function returns. ScriptEventBus::DrainDeferred
    // is the opposite: it delivers events queued by a PRIOR, separate
    // EmitDeferred call, at a LATER frame-boundary sync point
    // (ScriptRuntimeSceneSystem::DispatchDeferredEvents) — never within the
    // call that queued them. See ScriptEventBus.hpp's own "Dispatch mode
    // contract" doc comment for the full synchronous-vs-deferred contract;
    // this function and DrainEvents below are UNRELATED to that contract —
    // they predate ScriptEventBus (LIB-105) and are always, unconditionally
    // synchronous, with no deferred counterpart of their own.
    [[nodiscard]] ScriptRuntimeExecutionResult DispatchEventAndDrain(
        kb::scene::Scene& scene,
        const ScriptEvent& event,
        float deltaSeconds,
        ScriptRuntimeDispatchOptions options = {});

    // Delivers the events a script emitted, and those their handlers emit in turn (bounded by maxEventDepth).
    [[nodiscard]] ScriptRuntimeExecutionResult DrainEvents(
        kb::scene::Scene& scene,
        std::vector<ScriptEvent> initialEvents,
        float deltaSeconds,
        ScriptRuntimeDispatchOptions options = {});

private:
    [[nodiscard]] static std::size_t BackendIndex(kb::scene::BehaviourBackend backend) noexcept;
    static void MergeResult(ScriptRuntimeExecutionResult& target, ScriptRuntimeExecutionResult source);

    // The receivers of a broadcast (an event without a target) of one name: the behaviours whose script handles it, in
    // execution order, each with the call its backend resolved. Kept between broadcasts and built again only when the
    // scene's behaviours, a backend's calls or a script's answer changed.
    struct EventReceiver {
        kb::scene::SceneEntity entity{};
        kb::scene::BehaviourComponent behaviour{};
        ScriptPhaseCall call{};
    };
    struct EventScriptAnswer {
        std::uint64_t assetId = 0U;
        kb::scene::BehaviourBackend backend = kb::scene::BehaviourBackend::Native;
        bool handles = true;
    };
    struct EventReceiverList {
        std::vector<EventReceiver> receivers;
        std::vector<EventScriptAnswer> answers;
        // Behaviours a broadcast passes over: scripts without the handler and disabled behaviours.
        std::size_t passedOver = 0U;
        const kb::scene::Scene* scene = nullptr;
        std::uint64_t behaviourStamp = 0U;
        std::array<std::uint64_t, 3> callRevisions{};
        bool valid = false;
        bool inUse = false;
    };
    struct EventNameHash {
        using is_transparent = void;
        [[nodiscard]] std::size_t operator()(std::string_view name) const noexcept { return std::hash<std::string_view>{}(name); }
    };
    // Delivers a broadcast through the kept receiver list of its name; false when that list is being delivered already
    // (a handler that dispatches the same event again), and the caller then collects the receivers itself.
    [[nodiscard]] bool BroadcastToReceivers(kb::scene::Scene& scene, const ScriptEvent& event, float deltaSeconds, ScriptRuntimeExecutionResult& result);
    void RefreshEventReceivers(kb::scene::Scene& scene, std::string_view name, EventReceiverList& list);

    std::array<std::unique_ptr<IScriptBackend>, 3> backends_{};
    std::unordered_map<std::string, EventReceiverList, EventNameHash, std::equal_to<>> eventReceivers_;
    ScriptSharedState sharedState_;
    ScriptFunctionRegistry functions_;
    ScriptEventBus events_;
};

} // namespace kb::script
