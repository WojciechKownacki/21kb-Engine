#include "engine/script/ScriptRuntime.hpp"

#include "engine/scene/BehaviourExecutionOrder.hpp"
#include "engine/scene/SceneBehaviourComponents.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
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

// The context and the buffer for emitted events a dispatch of several behaviours shares: one object aimed at each
// behaviour in turn.
struct DispatchTarget {
    std::vector<ScriptEvent> emittedEvents;
    ScriptExecutionContext context;

    explicit DispatchTarget(const DispatchContext& dispatch)
        : context(
              *dispatch.scene,
              kb::scene::SceneEntity{},
              kb::assets::AssetId{},
              kb::scene::BehaviourBackend::Native,
              dispatch.lifecycle,
              dispatch.deltaSeconds,
              &emittedEvents,
              dispatch.event,
              dispatch.sharedState,
              dispatch.functions,
              dispatch.runtime == nullptr ? nullptr : &dispatch.runtime->Events()) {}
    DispatchTarget(const DispatchTarget&) = delete;
    DispatchTarget& operator=(const DispatchTarget&) = delete;
};

void DispatchBehaviour(kb::scene::SceneEntity entity, const kb::scene::BehaviourComponent& behaviour, DispatchContext& dispatch, DispatchTarget& target) {
    ++dispatch.result->visitedBehaviours;
    if (ShouldSkipDisabled(behaviour, dispatch)) {
        return;
    }

    IScriptBackend* backend = dispatch.runtime->FindBackend(behaviour.backend);
    if (backend == nullptr) {
        AddMissingBackendDiagnostic(dispatch, entity, behaviour);
        return;
    }

    ScriptExecutionContext& scriptContext = target.context;
    std::vector<ScriptEvent>& emittedEvents = target.emittedEvents;
    scriptContext.Rebind(entity, kb::assets::AssetId{ behaviour.behaviourAssetId }, behaviour.backend);

    const ScriptBackendExecutionResult backendResult = dispatch.event == nullptr
        ? backend->ExecuteLifecycle(behaviour, scriptContext)
        : backend->ExecuteEvent(behaviour, *dispatch.event, dispatch.eventId, scriptContext);
    if (backendResult.executed && backendResult.Succeeded()) {
        ++dispatch.result->executedBehaviours;
    }
    if (!emittedEvents.empty()) {
        AppendEvents(dispatch.result->emittedEvents, std::move(emittedEvents));
        emittedEvents.clear();
    }
    if (!backendResult.diagnostics.empty()) {
        AppendDiagnostics(dispatch.result->diagnostics, backendResult.diagnostics);
    }
}

void DispatchBehaviour(kb::scene::SceneEntity entity, const kb::scene::BehaviourComponent& behaviour, DispatchContext& dispatch) {
    DispatchTarget target{ dispatch };
    DispatchBehaviour(entity, behaviour, dispatch, target);
}

// The behaviours a broadcast collects: the storage is kept between broadcasts, so each one does not allocate the list
// of every behaviour in the scene again. A broadcast started while another one uses it (a handler that dispatches
// events) collects into a list of its own.
struct RecordScratch {
    std::vector<BehaviourDispatchRecord> records;
    bool inUse = false;
};

[[nodiscard]] RecordScratch& Scratch() noexcept {
    thread_local RecordScratch scratch;
    return scratch;
}

// Brackets the delivery of one event to many behaviours: each backend may build its value of the event once for all of
// them.
class EventBroadcastScope final {
public:
    EventBroadcastScope(ScriptRuntime& runtime, const ScriptEvent& event) {
        for (std::size_t index = 0U; index < backends_.size(); ++index) {
            backends_[index] = runtime.FindBackend(static_cast<kb::scene::BehaviourBackend>(index));
            if (backends_[index] != nullptr) {
                backends_[index]->BeginEventBroadcast(event);
            }
        }
    }
    ~EventBroadcastScope() {
        for (IScriptBackend* backend : backends_) {
            if (backend != nullptr) {
                backend->EndEventBroadcast();
            }
        }
    }
    EventBroadcastScope(const EventBroadcastScope&) = delete;
    EventBroadcastScope& operator=(const EventBroadcastScope&) = delete;

private:
    std::array<IScriptBackend*, 3> backends_{};
};

void DispatchSceneBehaviours(kb::scene::Scene& scene, DispatchContext& context) {
    if (context.event != nullptr && context.event->target.IsValid()) {
        // An event for one entity concerns that entity's behaviour only; there is nothing to collect and order.
        const kb::scene::SceneEntity target = context.event->target;
        if (const kb::scene::BehaviourComponent* behaviour = std::as_const(scene).Components().Behaviours().TryGet(target);
            behaviour != nullptr && scene.Entities().IsActive(target)) {
            const kb::scene::BehaviourComponent snapshot = *behaviour;
            DispatchBehaviour(target, snapshot, context);
        }
        return;
    }
    std::vector<BehaviourDispatchRecord> ownRecords;
    RecordScratch& scratch = Scratch();
    const bool leased = !scratch.inUse;
    std::vector<BehaviourDispatchRecord>& behaviours = leased ? scratch.records : ownRecords;
    if (leased) {
        scratch.inUse = true;
        behaviours.clear();
    }
    struct Lease {
        RecordScratch& scratch;
        bool leased;
        ~Lease() {
            if (leased) {
                scratch.inUse = false;
            }
        }
    } lease{ scratch, leased };
    scene.Components().Behaviours().ForEach(&CollectBehaviour, &behaviours);
    if (context.event != nullptr) {
        // An event reaches the behaviours whose script handles it. The rest are passed over before the order is
        // worked out, so a broadcast costs what its receivers cost, not a call for every behaviour in the scene;
        // each asset is asked once.
        struct Answer {
            std::uint64_t assetId = 0U;
            kb::scene::BehaviourBackend backend = kb::scene::BehaviourBackend::Native;
            bool handles = true;
        };
        std::array<Answer, 16> answers{};
        std::size_t answerCount = 0U;
        std::size_t passedOver = 0U;
        std::erase_if(behaviours, [&](const BehaviourDispatchRecord& record) {
            for (std::size_t index = 0U; index < answerCount; ++index) {
                if (answers[index].assetId == record.behaviour.behaviourAssetId && answers[index].backend == record.behaviour.backend) {
                    passedOver += answers[index].handles ? 0U : 1U;
                    return !answers[index].handles;
                }
            }
            IScriptBackend* backend = context.runtime->FindBackend(record.behaviour.backend);
            const bool handles = backend == nullptr || backend->HasEntry(record.behaviour, context.event->name);
            if (answerCount < answers.size()) {
                answers[answerCount++] = Answer{ record.behaviour.behaviourAssetId, record.behaviour.backend, handles };
            }
            passedOver += handles ? 0U : 1U;
            return !handles;
        });
        context.result->visitedBehaviours += passedOver;
    }
    // Behaviours usually come out of the storage in their execution order already; sorting them again is then a waste.
    if (!std::ranges::is_sorted(behaviours, &ComesBefore)) {
        std::ranges::sort(behaviours, &ComesBefore);
    }
    std::optional<EventBroadcastScope> broadcast;
    if (context.event != nullptr && !behaviours.empty()) {
        broadcast.emplace(*context.runtime, *context.event);
    }
    DispatchTarget target{ context };
    for (const BehaviourDispatchRecord& record : behaviours) {
        if (!scene.Entities().IsActive(record.entity)) {
            continue;
        }
        if (context.event != nullptr && context.event->target.IsValid() && record.entity != context.event->target) {
            continue;
        }
        DispatchBehaviour(record.entity, record.behaviour, context, target);
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

ScriptEventBus& ScriptRuntime::Events() noexcept {
    return events_;
}

const ScriptEventBus& ScriptRuntime::Events() const noexcept {
    return events_;
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
    if (result.emittedEvents.empty()) {
        return result;
    }
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
    // LIB-108: reject an oversized payload BEFORE dispatching to any
    // behaviour — this is the ONE dispatch entry every event delivery path
    // in the engine funnels through (ScriptExecutionContext::Emit/EmitTo,
    // Visual Graph EmitEvent/EmitEventTo, and every engine-emitted event —
    // TimerFired/TaskCompleted/TaskFailed/scene lifecycle — LIB-103's
    // confirmed "exactly one delivery mechanism"), so one check here covers
    // all of them; ScriptEventBus::Emit has its own identical check for the
    // separate bus (LIB-105) delivery path, which does not reach this
    // function.
    if (event.arguments.size() > kMaxScriptEventArguments) {
        result.diagnostics.push_back(ScriptDiagnostic{
            .message = "script event \"" + event.name + "\" exceeds the maximum of " + std::to_string(kMaxScriptEventArguments) + " arguments (" + std::to_string(event.arguments.size()) + " given)",
        });
        return result;
    }
    // LIB-098: Task.WaitEvent observes both explicit ScriptEventBus emits
    // and the engine's original behaviour dispatch path (timers, tasks,
    // collisions, scene lifecycle, and events emitted by behaviours).
    // Observation is separate from bus subscriptions, so this does not
    // duplicate callback delivery or change the established dispatch mode.
    if (!event.observationAlreadyNotified) {
        events_.NotifyObservations(event.name, event.target);
    }
    if (!event.target.IsValid() && BroadcastToReceivers(scene, event, deltaSeconds, result)) {
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

void ScriptRuntime::RefreshEventReceivers(kb::scene::Scene& scene, std::string_view name, EventReceiverList& list) {
    // Whether a script handles the event is asked once per script for every broadcast, so a handler defined or removed
    // while the game runs is seen at the next one; the behaviours themselves are only gone through again when one of
    // them, or a call a backend resolved, changed.
    bool changed = !list.valid || list.scene != &scene || list.behaviourStamp != std::as_const(scene).Components().Behaviours().ChangeStamp();
    for (std::size_t index = 0U; index < list.callRevisions.size(); ++index) {
        const IScriptBackend* backend = FindBackend(static_cast<kb::scene::BehaviourBackend>(index));
        changed = changed || list.callRevisions[index] != (backend == nullptr ? 0U : backend->PhaseCallRevision());
    }
    for (EventScriptAnswer& answer : list.answers) {
        kb::scene::BehaviourComponent probe{};
        probe.behaviourAssetId = answer.assetId;
        probe.backend = answer.backend;
        IScriptBackend* backend = FindBackend(answer.backend);
        const bool handles = backend == nullptr || backend->HasEntry(probe, name);
        changed = changed || handles != answer.handles;
    }
    if (!changed) {
        return;
    }

    RecordScratch& scratch = Scratch();
    std::vector<BehaviourDispatchRecord> ownRecords;
    const bool leased = !scratch.inUse;
    std::vector<BehaviourDispatchRecord>& behaviours = leased ? scratch.records : ownRecords;
    behaviours.clear();
    scene.Components().Behaviours().ForEach(&CollectBehaviour, &behaviours);
    if (!std::ranges::is_sorted(behaviours, &ComesBefore)) {
        std::ranges::sort(behaviours, &ComesBefore);
    }
    list.answers.clear();
    list.receivers.clear();
    list.passedOver = 0U;
    for (const BehaviourDispatchRecord& record : behaviours) {
        auto answer = std::ranges::find_if(list.answers, [&record](const EventScriptAnswer& known) {
            return known.assetId == record.behaviour.behaviourAssetId && known.backend == record.behaviour.backend;
        });
        IScriptBackend* backend = FindBackend(record.behaviour.backend);
        if (answer == list.answers.end()) {
            list.answers.push_back(EventScriptAnswer{ record.behaviour.behaviourAssetId, record.behaviour.backend,
                backend == nullptr || backend->HasEntry(record.behaviour, name) });
            answer = list.answers.end() - 1;
        }
        if (!answer->handles) {
            ++list.passedOver;
            continue;
        }
        // A disabled behaviour, or one without a backend, keeps no call: the broadcast hands it to the general dispatch,
        // which passes over the first and reports the second.
        list.receivers.push_back(EventReceiver{ .entity = record.entity, .behaviour = record.behaviour,
            .call = backend == nullptr || !record.behaviour.enabled ? ScriptPhaseCall{} : backend->ResolveEventCall(record.entity, record.behaviour, name) });
    }
    if (leased) {
        behaviours.clear();
    }
    // Resolving can make objects, so the revisions are read after it.
    for (std::size_t index = 0U; index < list.callRevisions.size(); ++index) {
        const IScriptBackend* backend = FindBackend(static_cast<kb::scene::BehaviourBackend>(index));
        list.callRevisions[index] = backend == nullptr ? 0U : backend->PhaseCallRevision();
    }
    list.scene = &scene;
    list.behaviourStamp = std::as_const(scene).Components().Behaviours().ChangeStamp();
    list.valid = true;
}

// A broadcast reaches the receivers its kept list names, in their order, each through the call its backend resolved -
// unless a handler changed a behaviour in the meantime: the receivers after that are delivered the way a dispatch
// that collects them is (each through its backend, skipping objects that are no longer active).
bool ScriptRuntime::BroadcastToReceivers(kb::scene::Scene& scene, const ScriptEvent& event, float deltaSeconds, ScriptRuntimeExecutionResult& result) {
    auto found = eventReceivers_.find(std::string_view{ event.name });
    if (found == eventReceivers_.end()) {
        found = eventReceivers_.try_emplace(event.name).first;
    }
    EventReceiverList& list = found->second;
    if (list.inUse) {
        return false;
    }
    RefreshEventReceivers(scene, found->first, list);
    result.visitedBehaviours += list.passedOver;
    if (list.receivers.empty()) {
        return true;
    }
    list.inUse = true;
    struct InUse {
        EventReceiverList& list;
        ~InUse() { list.inUse = false; }
    } inUse{ list };
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
    const EventBroadcastScope broadcast{ *this, event };
    DispatchTarget target{ context };
    const std::uint64_t* const revision = scene.Components().Behaviours().RevisionCounter();
    const std::uint64_t seenRevision = *revision;
    for (const EventReceiver& receiver : list.receivers) {
        if (!scene.Entities().IsActive(receiver.entity)) {
            continue;
        }
        if (receiver.call.invoke == nullptr || *revision != seenRevision) {
            DispatchBehaviour(receiver.entity, receiver.behaviour, context, target);
            continue;
        }
        ++result.visitedBehaviours;
        target.context.Rebind(receiver.entity, kb::assets::AssetId{ receiver.behaviour.behaviourAssetId }, receiver.behaviour.backend);
        if (receiver.call.invoke(receiver.call.target, receiver.behaviour, target.context, result.diagnostics)) {
            ++result.executedBehaviours;
        }
        target.context.FinishSelfTransform();
        if (!target.emittedEvents.empty()) {
            AppendEvents(result.emittedEvents, std::move(target.emittedEvents));
            target.emittedEvents.clear();
        }
    }
    return true;
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
