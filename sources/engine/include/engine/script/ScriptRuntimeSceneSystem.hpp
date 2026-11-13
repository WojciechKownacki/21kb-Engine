#pragma once

#include "engine/scene/SceneSystem.hpp"
#include "engine/scene/PhysicsBackend.hpp"
#include "engine/script/ScriptRuntime.hpp"
#include "engine/script/ScriptRuntimeAssetPreparer.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace kb::script {

// A widget event the script system delivered: the callback (OnUIClicked) or the Selectable's event name, the
// widget it is about, the object that received it and how many behaviours ran a function for it.
struct ScriptUIDispatch {
    std::string name;
    kb::scene::SceneEntity widget{};
    kb::scene::SceneEntity target{};
    std::uint32_t answered = 0U;
};

struct ScriptRuntimeFrameSettings {
    float fixedDeltaSeconds = 1.0F / 60.0F;
    std::size_t maxFixedStepsPerFrame = 64U;
};

class ScriptRuntimeSceneSystem final : public kb::scene::SceneSystem {
public:
    explicit ScriptRuntimeSceneSystem(ScriptRuntime& runtime) noexcept;
    ScriptRuntimeSceneSystem(ScriptRuntime& runtime, ScriptRuntimeAssetPreparer& assetPreparer) noexcept;

    void OnCreate(kb::scene::SceneSystemContext& context) override;
    void OnFrameStart(kb::scene::SceneSystemContext& context) override;
    void OnUpdate(kb::scene::SceneSystemContext& context) override;
    void OnFixedUpdate(kb::scene::SceneSystemContext& context) override;
    void OnDestroy(kb::scene::SceneSystemContext& context) override;
    [[nodiscard]] bool RequiresFixedStep() const override { return true; }
    [[nodiscard]] kb::scene::SceneUpdatePhase UpdatePhase() const noexcept override {
        return kb::scene::SceneUpdatePhase::PostFixed;
    }
    [[nodiscard]] kb::scene::SceneFixedUpdatePhase FixedUpdatePhase() const noexcept override {
        return kb::scene::SceneFixedUpdatePhase::PreSimulation;
    }

    [[nodiscard]] const ScriptRuntimeExecutionResult& ExecuteStartup(kb::scene::Scene& scene, float deltaSeconds);
    [[nodiscard]] const ScriptRuntimeExecutionResult& ExecuteFrame(kb::scene::Scene& scene, float deltaSeconds);
    [[nodiscard]] const ScriptRuntimeExecutionResult& ExecuteShutdown(kb::scene::Scene& scene, float deltaSeconds);
    [[nodiscard]] const ScriptRuntimeExecutionResult& ExecutePhase(kb::scene::Scene& scene, ScriptLifecycleEvent event, float deltaSeconds);

    void SetFrameSettings(ScriptRuntimeFrameSettings settings) noexcept;
    [[nodiscard]] ScriptRuntimeFrameSettings FrameSettings() const noexcept;

    [[nodiscard]] const ScriptRuntimeExecutionResult& LastResult() const noexcept;
    [[nodiscard]] const ScriptRuntimeAssetPrepareResult& LastPrepareResult() const noexcept;
    // The widget events delivered since the last call (the newest 256), for tools that report who answered a click.
    [[nodiscard]] std::vector<ScriptUIDispatch> TakeUIDispatches();

private:
    friend class ScriptRuntimeHost;
    friend struct ScriptRuntimeHostState;
    struct BehaviourLifecycleKey {
        std::uint64_t entityId = 0;
        std::uint64_t assetId = 0;
        kb::scene::BehaviourBackend backend = kb::scene::BehaviourBackend::Native;

        [[nodiscard]] friend constexpr bool operator==(BehaviourLifecycleKey lhs, BehaviourLifecycleKey rhs) noexcept = default;
    };

    struct BehaviourLifecycleKeyHasher {
        [[nodiscard]] std::size_t operator()(BehaviourLifecycleKey key) const noexcept;
    };

    struct BehaviourLifecycleRecord {
        kb::scene::SceneEntity entity{};
        kb::scene::BehaviourComponent behaviour{};
        bool active = false;
        bool created = false;
        // The synchronization pass that last found the behaviour in the scene; a record an older pass left
        // behind belongs to a behaviour that is gone.
        std::uint64_t seenEpoch = 0U;
    };

    void BeginFrame(kb::scene::Scene& scene, float deltaSeconds);
    void ExecuteVariableFrame(kb::scene::Scene& scene, float deltaSeconds);
    void ExecuteFixedStep(kb::scene::Scene& scene, float fixedDeltaSeconds);
    void ConfigureSceneFixedStep(kb::scene::Scene& scene) noexcept;
    void PrepareScene(kb::scene::Scene& scene);
    void RestartReloadedBehaviours(kb::scene::Scene& scene);
    void ExecuteTrackedBehaviourPhase(kb::scene::Scene& scene, ScriptLifecycleEvent event, float deltaSeconds);
    // LIB-073: drains kb::scene::SceneLoadedContent's pending
    // SceneLoading/SceneLoaded/SceneActivated/SceneUnloading/SceneUnloaded
    // notifications and turns each into a real ScriptEvent broadcast via
    // ScriptRuntime::DispatchEventAndDrain — called before
    // SyncBehaviourLifecycles so "the scene finished loading" is observed
    // before any newly-instantiated entities' own Created/Activated/Ready
    // lifecycle fires in the same pass.
    void DispatchPendingSceneLifecycleEvents(kb::scene::Scene& scene, float deltaSeconds);
    // LIB-095: advances every Timer.Once/Timer.Repeat live in the scene
    // (kb::scene::SceneTimers::Advance) and turns each one that fired into
    // a real, targeted "TimerFired" ScriptEvent — mirrors
    // DispatchPendingSceneLifecycleEvents' own drain-and-broadcast shape.
    void DispatchFiredTimers(kb::scene::Scene& scene, float deltaSeconds);
    // LIB-097: polls every native-started Task live in the scene
    // (kb::scene::SceneTasks::Advance) and turns each one that reached a
    // terminal state into a real, targeted "TaskCompleted"/"TaskFailed"
    // ScriptEvent — same shape as DispatchFiredTimers above.
    void DispatchCompletedTasks(kb::scene::Scene& scene, float deltaSeconds);
    // LIB-098: identical shape to DispatchCompletedTasks above, but drives
    // kb::scene::SceneTasks::AdvanceFixedSteps (StartFixedStep tasks)
    // instead of Advance. Production calls it after each authoritative scene
    // FixedTick; direct ExecuteFrame uses the same per-step path.
    void DispatchCompletedFixedStepTasks(kb::scene::Scene& scene, std::size_t stepCount, float deltaSeconds);
    void DispatchPendingParticleEvents(kb::scene::Scene& scene, float deltaSeconds);
    // LIB-105: delivers every kb::script::ScriptEventBus::EmitDeferred call
    // queued since the last frame — the deferred half of the pub/sub bus's
    // sync/deferred pair. Unlike DispatchFiredTimers/DispatchCompletedTasks
    // above, this does not go through ScriptRuntime::DispatchEventAndDrain
    // (there is no behaviour to visit — subscriptions are not behaviours);
    // any subscriber exception surfaces as a ScriptDiagnostic in
    // lastResult_ instead, since ScriptEventBus::Emit already converts it
    // to a plain error string rather than letting it escape.
    void DispatchDeferredEvents(kb::scene::Scene& scene);
    // LIB-127: drains kb::scene::PhysicsBackend's pending collision/trigger
    // events (queued by whichever physics plugin is loaded, e.g.
    // kb_physics_jolt_plugin's contact listener) and turns each into a
    // real, entity-local ("target"=the entity the callback is for)
    // "OnCollisionEnter"/"OnCollisionStay"/"OnCollisionExit"/
    // "OnTriggerEnter"/"OnTriggerStay"/"OnTriggerExit" ScriptEvent — same
    // drain-and-dispatch shape as DispatchFiredTimers/DispatchCompletedTasks
    // above.
    void DispatchPendingCollisionEvents(kb::scene::Scene& scene, float deltaSeconds);
    // LIB-152: fired audio voice markers -> ENTITY-LOCAL "OnAudioMarker" events, the same
    // drain-and-dispatch shape as the collision events above.
    void DispatchPendingAudioMarkerEvents(kb::scene::Scene& scene, float deltaSeconds);
    // LIB-160: queued prefab-instantiation completions -> ENTITY-LOCAL
    // "OnPrefabInstantiated" events on the requesting caller, same
    // drain-and-dispatch shape as the collision/marker events above.
    void DispatchPendingPrefabInstantiatedEvents(kb::scene::Scene& scene, float deltaSeconds);
    // LIB-168: fixed-name, versioned OnAnimationEvent dispatch. Authored
    // marker ids remain typed Hash payload data and are never invoked as
    // reflected method names.
    void DispatchPendingAnimationEvents(kb::scene::Scene& scene, float deltaSeconds);
    void DispatchPendingGameplayEvents(kb::scene::Scene& scene, float deltaSeconds);
    void DispatchPendingTimelineMarkerEvents(
        kb::scene::Scene& scene, float deltaSeconds);
    void DispatchPendingUIEvents(kb::scene::Scene& scene, float deltaSeconds);
    void SyncBehaviourLifecycles(kb::scene::Scene& scene, float deltaSeconds);
    void ShutdownTrackedBehaviours(kb::scene::Scene& scene, float deltaSeconds);
    void DispatchDeactivateAndDestroyInOrder(kb::scene::Scene& scene, std::vector<BehaviourLifecycleRecord>& records, float deltaSeconds);
    void ExecuteBehaviourPhase(
        kb::scene::Scene& scene,
        kb::scene::SceneEntity entity,
        const kb::scene::BehaviourComponent& behaviour,
        ScriptLifecycleEvent event,
        float deltaSeconds);
    [[nodiscard]] static BehaviourLifecycleKey MakeKey(kb::scene::SceneEntity entity, const kb::scene::BehaviourComponent& behaviour) noexcept;

    std::vector<kb::scene::PendingCollisionEvent> collisionEventScratch_;
    std::uint64_t lastGameplayEventSequence_ = 0U;
    ScriptRuntime& runtime_;
    ScriptRuntimeAssetPreparer* assetPreparer_ = nullptr;
    kb::scene::Scene* attachedScene_ = nullptr;
    ScriptRuntimeFrameSettings frameSettings_{};
    float fixedAccumulatorSeconds_ = 0.0F;
    ScriptRuntimeExecutionResult lastResult_;
    ScriptRuntimeAssetPrepareResult lastPrepareResult_;
    std::vector<ScriptUIDispatch> uiDispatches_;
    std::unordered_map<BehaviourLifecycleKey, BehaviourLifecycleRecord, BehaviourLifecycleKeyHasher> lifecycleRecords_;
    // The records the per-frame phases visit (created and active), in the guaranteed execution order. Built when a
    // synchronization or a restart changes the records, not for every phase of every frame; the pointers refer to
    // lifecycleRecords_ elements, so whoever replaces or erases records marks the order dirty.
    std::vector<BehaviourLifecycleRecord*> phaseOrder_;
    bool phaseOrderDirty_ = true;
    // The scripts the ordered behaviours run, once each, so a phase that none of them has an entry for is skipped
    // whole. When there are more distinct scripts than it keeps, every behaviour is asked as before.
    struct PhaseScript {
        std::uint64_t assetId = 0U;
        kb::scene::BehaviourBackend backend = kb::scene::BehaviourBackend::Native;
    };
    std::vector<PhaseScript> phaseScripts_;
    bool phaseScriptsOverflow_ = false;
    std::uint64_t syncEpoch_ = 0U;
};

} // namespace kb::script
