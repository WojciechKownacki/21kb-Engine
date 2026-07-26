#include "engine/script/ScriptRuntimeSceneSystem.hpp"

#include "engine/scene/BehaviourExecutionOrder.hpp"
#include "engine/audio/AudioPlayback.hpp"
#include "engine/scene/PhysicsBackend.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneBehaviourComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneLoadedContent.hpp"
#include "engine/scene/SceneParticleSystems.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneSystemContext.hpp"
#include "engine/scene/SceneTasks.hpp"
#include "engine/scene/SceneTimers.hpp"

#include <algorithm>
#include <unordered_set>
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

struct RawBehaviourRecord {
    kb::scene::SceneEntity entity{};
    kb::scene::BehaviourComponent behaviour{};
};

struct BehaviourCollectContext {
    std::vector<RawBehaviourRecord>* records = nullptr;
};

void CollectBehaviour(kb::scene::SceneEntity entity, const kb::scene::BehaviourComponent& behaviour, void* rawContext) {
    auto& context = *static_cast<BehaviourCollectContext*>(rawContext);
    context.records->push_back(RawBehaviourRecord{
        .entity = entity,
        .behaviour = behaviour,
    });
}

} // namespace

std::size_t ScriptRuntimeSceneSystem::BehaviourLifecycleKeyHasher::operator()(BehaviourLifecycleKey key) const noexcept {
    std::uint64_t hash = key.entityId;
    hash ^= key.assetId + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    hash ^= static_cast<std::uint64_t>(key.backend) + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return static_cast<std::size_t>(hash);
}

ScriptRuntimeSceneSystem::ScriptRuntimeSceneSystem(ScriptRuntime& runtime) noexcept
    : runtime_(runtime) {}

ScriptRuntimeSceneSystem::ScriptRuntimeSceneSystem(ScriptRuntime& runtime, ScriptRuntimeAssetPreparer& assetPreparer) noexcept
    : runtime_(runtime)
    , assetPreparer_(&assetPreparer) {}

void ScriptRuntimeSceneSystem::OnCreate(kb::scene::SceneSystemContext& context) {
    attachedScene_ = &context.GetScene();
    ConfigureSceneFixedStep(*attachedScene_);
    static_cast<void>(ExecuteStartup(context.GetScene(), context.DeltaSeconds()));
}

void ScriptRuntimeSceneSystem::OnFrameStart(kb::scene::SceneSystemContext& context) {
    BeginFrame(context.GetScene(), context.DeltaSeconds());
}

void ScriptRuntimeSceneSystem::OnUpdate(kb::scene::SceneSystemContext& context) {
    ExecuteVariableFrame(context.GetScene(), context.DeltaSeconds());
}

void ScriptRuntimeSceneSystem::OnFixedUpdate(kb::scene::SceneSystemContext& context) {
    ExecuteFixedStep(context.GetScene(), context.DeltaSeconds());
}

void ScriptRuntimeSceneSystem::OnDestroy(kb::scene::SceneSystemContext& context) {
    static_cast<void>(ExecuteShutdown(context.GetScene(), context.DeltaSeconds()));
    attachedScene_ = nullptr;
}

const ScriptRuntimeExecutionResult& ScriptRuntimeSceneSystem::ExecuteStartup(kb::scene::Scene& scene, float deltaSeconds) {
    lastResult_ = {};
    PrepareScene(scene);
    DispatchPendingSceneLifecycleEvents(scene, deltaSeconds);
    SyncBehaviourLifecycles(scene, deltaSeconds);
    // LIB-067: drain here too so a deferred destroy queued from an OnStart /
    // startup-phase behaviour is applied before the first ExecuteFrame.
    static_cast<void>(scene.Entities().DrainDeferredDestroys());
    return lastResult_;
}

const ScriptRuntimeExecutionResult& ScriptRuntimeSceneSystem::ExecuteFrame(kb::scene::Scene& scene, float deltaSeconds) {
    lastResult_ = {};
    PrepareScene(scene);
    const float clampedDeltaSeconds = std::max(deltaSeconds, 0.0F);
    DispatchPendingSceneLifecycleEvents(scene, clampedDeltaSeconds);
    DispatchFiredTimers(scene, clampedDeltaSeconds);
    DispatchCompletedTasks(scene, clampedDeltaSeconds);
    AdvanceParticleSystems(scene, clampedDeltaSeconds);
    DispatchPendingCollisionEvents(scene, clampedDeltaSeconds);
    DispatchPendingAudioMarkerEvents(scene, clampedDeltaSeconds);
    DispatchPendingPrefabInstantiatedEvents(scene, clampedDeltaSeconds);
    DispatchDeferredEvents(scene);
    SyncBehaviourLifecycles(scene, clampedDeltaSeconds);
    // LIB-094: explicit FixedTick-during-pause rule — while the scene is
    // paused (Runtime().IsPlaying()==false), wall-clock time is NOT
    // accumulated into fixedAccumulatorSeconds_ at all, so FixedTick simply
    // never fires for the duration of the pause and no step "debt" builds
    // up (unpausing does not trigger a burst of catch-up steps). Tick/
    // LateTick/BeforeRender/AfterRender below are deliberately NOT gated
    // here — they keep firing while paused (e.g. so pause-menu/UI
    // behaviours keep working), the gameplay freeze instead comes from
    // Time.Delta reading 0 while paused (ScriptTimeApi.cpp).
    if (scene.Runtime().IsPlaying()) {
        fixedAccumulatorSeconds_ += clampedDeltaSeconds;
    }
    std::size_t fixedSteps = 0U;
    while (frameSettings_.fixedDeltaSeconds > 0.0F && fixedAccumulatorSeconds_ >= frameSettings_.fixedDeltaSeconds && fixedSteps < frameSettings_.maxFixedStepsPerFrame) {
        fixedAccumulatorSeconds_ -= frameSettings_.fixedDeltaSeconds;
        ++fixedSteps;
        ExecuteTrackedBehaviourPhase(scene, ScriptLifecycleEvent::FixedTick, frameSettings_.fixedDeltaSeconds);
    }
    if (frameSettings_.fixedDeltaSeconds <= 0.0F || frameSettings_.maxFixedStepsPerFrame == 0U ||
        (fixedSteps == frameSettings_.maxFixedStepsPerFrame && fixedAccumulatorSeconds_ >= frameSettings_.fixedDeltaSeconds)) {
        fixedAccumulatorSeconds_ = 0.0F;
    }
    // LIB-098: fixedSteps is exactly what StartFixedStep tasks need and
    // nothing else in ExecuteFrame previously surfaced — dispatched right
    // after the fixed-step loop so a "wait N fixed steps" task's
    // TaskCompleted/TaskFailed lands before the variable Tick phase below.
    DispatchCompletedFixedStepTasks(scene, fixedSteps, clampedDeltaSeconds);
    ExecuteTrackedBehaviourPhase(scene, ScriptLifecycleEvent::Tick, clampedDeltaSeconds);
    ExecuteTrackedBehaviourPhase(scene, ScriptLifecycleEvent::LateTick, clampedDeltaSeconds);
    ExecuteTrackedBehaviourPhase(scene, ScriptLifecycleEvent::BeforeRender, clampedDeltaSeconds);
    ExecuteTrackedBehaviourPhase(scene, ScriptLifecycleEvent::AfterRender, clampedDeltaSeconds);
    // LIB-067: frame playback point — apply every World.Destroy(deferred=true)
    // queued during this frame's behaviour phases now that all iteration is
    // done, so no behaviour ran against storage a deferred destroy will pull.
    static_cast<void>(scene.Entities().DrainDeferredDestroys());
    return lastResult_;
}

void ScriptRuntimeSceneSystem::BeginFrame(kb::scene::Scene& scene, float deltaSeconds) {
    lastResult_ = {};
    PrepareScene(scene);
    const float clampedDeltaSeconds = std::max(deltaSeconds, 0.0F);
    DispatchPendingSceneLifecycleEvents(scene, clampedDeltaSeconds);
    DispatchFiredTimers(scene, clampedDeltaSeconds);
    DispatchCompletedTasks(scene, clampedDeltaSeconds);
    AdvanceParticleSystems(scene, clampedDeltaSeconds);
    DispatchPendingAudioMarkerEvents(scene, clampedDeltaSeconds);
    DispatchPendingPrefabInstantiatedEvents(scene, clampedDeltaSeconds);
    DispatchDeferredEvents(scene);
    SyncBehaviourLifecycles(scene, clampedDeltaSeconds);
}

void ScriptRuntimeSceneSystem::ExecuteFixedStep(kb::scene::Scene& scene, float fixedDeltaSeconds) {
    if (!scene.Runtime().IsPlaying()) {
        return;
    }
    scene.Runtime().SetScriptFixedDeltaSeconds(fixedDeltaSeconds);
    ExecuteTrackedBehaviourPhase(scene, ScriptLifecycleEvent::FixedTick, fixedDeltaSeconds);
    DispatchCompletedFixedStepTasks(scene, 1U, fixedDeltaSeconds);
}

void ScriptRuntimeSceneSystem::ExecuteVariableFrame(kb::scene::Scene& scene, float deltaSeconds) {
    const float clampedDeltaSeconds = std::max(deltaSeconds, 0.0F);
    // Physics contact events are queued during the Simulation phase. The
    // post-fixed script update drains them before Tick in the same frame.
    DispatchPendingCollisionEvents(scene, clampedDeltaSeconds);
    ExecuteTrackedBehaviourPhase(scene, ScriptLifecycleEvent::Tick, clampedDeltaSeconds);
    ExecuteTrackedBehaviourPhase(scene, ScriptLifecycleEvent::LateTick, clampedDeltaSeconds);
    ExecuteTrackedBehaviourPhase(scene, ScriptLifecycleEvent::BeforeRender, clampedDeltaSeconds);
    ExecuteTrackedBehaviourPhase(scene, ScriptLifecycleEvent::AfterRender, clampedDeltaSeconds);
    static_cast<void>(scene.Entities().DrainDeferredDestroys());
}

const ScriptRuntimeExecutionResult& ScriptRuntimeSceneSystem::ExecuteShutdown(kb::scene::Scene& scene, float deltaSeconds) {
    lastResult_ = {};
    ShutdownTrackedBehaviours(scene, deltaSeconds);
    return lastResult_;
}

const ScriptRuntimeExecutionResult& ScriptRuntimeSceneSystem::ExecutePhase(kb::scene::Scene& scene, ScriptLifecycleEvent event, float deltaSeconds) {
    PrepareScene(scene);
    lastResult_ = runtime_.ExecuteLifecycleAndDispatchEvents(scene, event, deltaSeconds);
    return lastResult_;
}

void ScriptRuntimeSceneSystem::SetFrameSettings(ScriptRuntimeFrameSettings settings) noexcept {
    frameSettings_ = settings;
    fixedAccumulatorSeconds_ = 0.0F;
    if (attachedScene_ != nullptr) {
        ConfigureSceneFixedStep(*attachedScene_);
    }
}

ScriptRuntimeFrameSettings ScriptRuntimeSceneSystem::FrameSettings() const noexcept {
    return frameSettings_;
}

const ScriptRuntimeExecutionResult& ScriptRuntimeSceneSystem::LastResult() const noexcept {
    return lastResult_;
}

const ScriptRuntimeAssetPrepareResult& ScriptRuntimeSceneSystem::LastPrepareResult() const noexcept {
    return lastPrepareResult_;
}

void ScriptRuntimeSceneSystem::PrepareScene(kb::scene::Scene& scene) {
    // LIB-093: stamp the scene with THIS system's script FixedTick delta so
    // Time.FixedDelta (ScriptTimeApi) reports the step a script's FixedTick
    // actually runs at — frameSettings_.fixedDeltaSeconds — rather than the
    // physics SceneRuntimeFixedStepSettings step, which is configured
    // independently. Done in PrepareScene because every script entry point
    // (ExecuteStartup/ExecuteFrame/ExecuteShutdown/ExecutePhase) routes
    // through it, so the stamp is fresh for every phase a script may call
    // Time.FixedDelta from, not only FixedTick.
    const float fixedDeltaSeconds = attachedScene_ == &scene
        ? scene.Runtime().FixedStepSettings().fixedDeltaSeconds
        : frameSettings_.fixedDeltaSeconds;
    scene.Runtime().SetScriptFixedDeltaSeconds(fixedDeltaSeconds);
    lastPrepareResult_ = assetPreparer_ == nullptr ? ScriptRuntimeAssetPrepareResult{} : assetPreparer_->PrepareSceneBehaviours(scene);
}

void ScriptRuntimeSceneSystem::ExecuteTrackedBehaviourPhase(
    kb::scene::Scene& scene,
    ScriptLifecycleEvent event,
    float deltaSeconds) {
    // lifecycleRecords_ is the authoritative frame-start set. Structural
    // changes are immediately visible to World APIs, but a Behaviour created
    // by one phase must not enter a later phase before the next BeginFrame has
    // prepared its asset and dispatched Created/Activated/Ready.
    std::vector<BehaviourLifecycleRecord> records;
    records.reserve(lifecycleRecords_.size());
    for (const auto& [key, tracked] : lifecycleRecords_) {
        (void)key;
        if (tracked.created && tracked.active) {
            records.push_back(tracked);
        }
    }
    std::ranges::sort(records, [](const BehaviourLifecycleRecord& lhs, const BehaviourLifecycleRecord& rhs) {
        return kb::scene::BehaviourExecutionOrderLess(lhs.entity, lhs.behaviour, rhs.entity, rhs.behaviour);
    });

    for (const BehaviourLifecycleRecord& tracked : records) {
        if (!scene.Entities().IsAlive(tracked.entity)) {
            continue;
        }
        const kb::scene::BehaviourComponent* live = scene.Components().Behaviours().TryGet(tracked.entity);
        if (live == nullptr || !live->enabled ||
            live->behaviourAssetId != tracked.behaviour.behaviourAssetId ||
            live->backend != tracked.behaviour.backend) {
            continue;
        }
        ExecuteBehaviourPhase(scene, tracked.entity, *live, event, deltaSeconds);
    }
}

void ScriptRuntimeSceneSystem::ConfigureSceneFixedStep(kb::scene::Scene& scene) noexcept {
    kb::scene::SceneRuntimeFixedStepSettings settings = scene.Runtime().FixedStepSettings();
    settings.fixedDeltaSeconds = frameSettings_.fixedDeltaSeconds;
    settings.maxFixedStepsPerFrame = frameSettings_.maxFixedStepsPerFrame;
    scene.Runtime().SetFixedStepSettings(settings);
    scene.Runtime().SetScriptFixedDeltaSeconds(settings.fixedDeltaSeconds);
}

void ScriptRuntimeSceneSystem::DispatchPendingSceneLifecycleEvents(kb::scene::Scene& scene, float deltaSeconds) {
    // LIB-103: a WORLD event (ScriptEvent.hpp's taxonomy) — target is never
    // set, so this always broadcasts to every enabled behaviour.
    for (kb::scene::SceneLifecycleEventRecord& pending : scene.LoadedContent().DrainPendingLifecycleEvents()) {
        ScriptEvent event;
        event.name = std::move(pending.name);
        event.arguments.push_back(ScriptEventArgument{ .name = "sceneId", .value = ScriptValue{ pending.sceneId, ScriptValueType::Hash } });
        event.arguments.push_back(ScriptEventArgument{ .name = "sceneName", .value = ScriptValue{ std::move(pending.sceneName) } });
        MergeResult(lastResult_, runtime_.DispatchEventAndDrain(scene, event, deltaSeconds));
    }
}

void ScriptRuntimeSceneSystem::DispatchFiredTimers(kb::scene::Scene& scene, float deltaSeconds) {
    // LIB-103: ENTITY-LOCAL if the timer had an owner (fired.owner valid),
    // WORLD otherwise (ScriptEvent.hpp's taxonomy) — see IsEntityLocalEvent/
    // IsWorldEvent (ScriptEventTaxonomy.hpp) for the canonical check.
    for (kb::scene::TimerFiredRecord& fired : scene.Timers().Advance(deltaSeconds)) {
        ScriptEvent event;
        event.name = "TimerFired";
        event.target = fired.owner;
        event.arguments.push_back(ScriptEventArgument{ .name = "timer", .value = ScriptValue{ fired.id, ScriptValueType::Hash } });
        MergeResult(lastResult_, runtime_.DispatchEventAndDrain(scene, event, deltaSeconds));
    }
}

void ScriptRuntimeSceneSystem::DispatchCompletedTasks(kb::scene::Scene& scene, float deltaSeconds) {
    // LIB-103: ENTITY-LOCAL or WORLD depending on completion.owner — same
    // taxonomy/convention as DispatchFiredTimers above.
    for (kb::scene::TaskCompletionRecord& completion : scene.Tasks().Advance(deltaSeconds)) {
        ScriptEvent event;
        event.name = completion.succeeded ? "TaskCompleted" : "TaskFailed";
        event.target = completion.owner;
        event.arguments.push_back(ScriptEventArgument{ .name = "task", .value = ScriptValue{ completion.id, ScriptValueType::Hash } });
        MergeResult(lastResult_, runtime_.DispatchEventAndDrain(scene, event, deltaSeconds));
    }
}

void ScriptRuntimeSceneSystem::AdvanceParticleSystems(kb::scene::Scene& scene, float deltaSeconds) {
    scene.Particles().Advance(deltaSeconds);
}

void ScriptRuntimeSceneSystem::DispatchPendingCollisionEvents(kb::scene::Scene& scene, float deltaSeconds) {
    // LIB-103: ENTITY-LOCAL (target is always the entity the callback is
    // for, exactly like DispatchFiredTimers/DispatchCompletedTasks above).
    // The event name encodes both the Enter/Stay/Exit phase and whether
    // either collider involved is a trigger (kb::scene::PendingCollisionEvent::
    // isTrigger) — the same six names Unity's own OnCollision*/OnTrigger*
    // callbacks use, so existing Lua/Native/VisualGraph scripts written
    // against that convention need no translation layer.
    for (const kb::scene::PendingCollisionEvent& pending : kb::scene::PhysicsBackend::DrainPendingCollisionEvents(scene)) {
        const char* name = nullptr;
        switch (pending.phase) {
        case kb::scene::PhysicsContactPhase::Enter:
            name = pending.isTrigger ? "OnTriggerEnter" : "OnCollisionEnter";
            break;
        case kb::scene::PhysicsContactPhase::Stay:
            name = pending.isTrigger ? "OnTriggerStay" : "OnCollisionStay";
            break;
        case kb::scene::PhysicsContactPhase::Exit:
            name = pending.isTrigger ? "OnTriggerExit" : "OnCollisionExit";
            break;
        }
        ScriptEvent event;
        event.name = name;
        event.target = pending.target;
        event.arguments.push_back(ScriptEventArgument{ .name = "other", .value = ScriptValue{ pending.other.Id(), ScriptValueType::Entity } });
        event.arguments.push_back(ScriptEventArgument{ .name = "pointX", .value = ScriptValue{ pending.point.x } });
        event.arguments.push_back(ScriptEventArgument{ .name = "pointY", .value = ScriptValue{ pending.point.y } });
        event.arguments.push_back(ScriptEventArgument{ .name = "pointZ", .value = ScriptValue{ pending.point.z } });
        event.arguments.push_back(ScriptEventArgument{ .name = "normalX", .value = ScriptValue{ pending.normal.x } });
        event.arguments.push_back(ScriptEventArgument{ .name = "normalY", .value = ScriptValue{ pending.normal.y } });
        event.arguments.push_back(ScriptEventArgument{ .name = "normalZ", .value = ScriptValue{ pending.normal.z } });
        MergeResult(lastResult_, runtime_.DispatchEventAndDrain(scene, event, deltaSeconds));
    }
}

void ScriptRuntimeSceneSystem::DispatchPendingAudioMarkerEvents(kb::scene::Scene& scene, float deltaSeconds) {
    // LIB-152: ENTITY-LOCAL, exactly like the collision events above - the target is the
    // entity whose script registered the marker (Audio.AddMarker's caller), and the
    // position argument is the voice's AUDIO-clock position at fire time.
    for (const kb::audio::PendingAudioMarkerEvent& pending : kb::audio::AudioPlayback::DrainPendingMarkerEvents(scene)) {
        ScriptEvent event;
        event.name = "OnAudioMarker";
        event.target = pending.target;
        event.arguments.push_back(ScriptEventArgument{ .name = "voice", .value = ScriptValue{ static_cast<int>(pending.voiceId) } });
        event.arguments.push_back(ScriptEventArgument{ .name = "marker", .value = ScriptValue{ pending.marker } });
        event.arguments.push_back(ScriptEventArgument{ .name = "positionSeconds", .value = ScriptValue{ pending.positionSeconds } });
        MergeResult(lastResult_, runtime_.DispatchEventAndDrain(scene, event, deltaSeconds));
    }
}

void ScriptRuntimeSceneSystem::DispatchPendingPrefabInstantiatedEvents(kb::scene::Scene& scene, float deltaSeconds) {
    // LIB-160: ENTITY-LOCAL, exactly like the collision/marker events above -
    // the target is the CALLER that requested the spawn (World.
    // InstantiatePrefab's context.caller), and the arguments carry the
    // instantiated root entity and the object count, so a spawn-manager
    // script gets a real "your prefab finished instantiating, here is its
    // root" completion callback.
    for (const kb::scene::ScenePrefabInstantiatedEventRecord& pending : scene.Prefabs().DrainPendingInstantiatedEvents()) {
        ScriptEvent event;
        event.name = "OnPrefabInstantiated";
        event.target = pending.caller;
        event.arguments.push_back(ScriptEventArgument{ .name = "root", .value = ScriptValue{ pending.root.Id(), ScriptValueType::Entity } });
        event.arguments.push_back(ScriptEventArgument{ .name = "count", .value = ScriptValue{ pending.count } });
        MergeResult(lastResult_, runtime_.DispatchEventAndDrain(scene, event, deltaSeconds));
    }
}

void ScriptRuntimeSceneSystem::DispatchDeferredEvents(kb::scene::Scene& scene) {
    const ScriptEventDeliveryResult result = runtime_.Events().DrainDeferred(scene);
    for (const std::string& error : result.errors) {
        lastResult_.diagnostics.push_back(ScriptDiagnostic{
            .message = error,
        });
    }
}

void ScriptRuntimeSceneSystem::DispatchCompletedFixedStepTasks(kb::scene::Scene& scene, std::size_t stepCount, float deltaSeconds) {
    // LIB-103: same ENTITY-LOCAL/WORLD taxonomy as DispatchCompletedTasks.
    for (kb::scene::TaskCompletionRecord& completion : scene.Tasks().AdvanceFixedSteps(stepCount)) {
        ScriptEvent event;
        event.name = completion.succeeded ? "TaskCompleted" : "TaskFailed";
        event.target = completion.owner;
        event.arguments.push_back(ScriptEventArgument{ .name = "task", .value = ScriptValue{ completion.id, ScriptValueType::Hash } });
        MergeResult(lastResult_, runtime_.DispatchEventAndDrain(scene, event, deltaSeconds));
    }
}

void ScriptRuntimeSceneSystem::SyncBehaviourLifecycles(kb::scene::Scene& scene, float deltaSeconds) {
    const std::vector<BehaviourLifecycleRecord> currentRecords = CollectBehaviourRecords(scene);
    std::unordered_set<BehaviourLifecycleKey, BehaviourLifecycleKeyHasher> seen;
    seen.reserve(currentRecords.size());

    for (const BehaviourLifecycleRecord& current : currentRecords) {
        const BehaviourLifecycleKey key = MakeKey(current.entity, current.behaviour);
        static_cast<void>(seen.insert(key));
        BehaviourLifecycleRecord& tracked = lifecycleRecords_[key];
        if (!tracked.entity.IsValid()) {
            tracked = BehaviourLifecycleRecord{
                .entity = current.entity,
                .behaviour = current.behaviour,
                .active = false,
                .created = false,
            };
        }

        if (current.behaviour.enabled) {
            tracked.entity = current.entity;
            tracked.behaviour = current.behaviour;
            if (!tracked.created) {
                // Seed editor-authored exposed-variable overrides into the backend
                // BEFORE Created runs, so the very first lifecycle hook already
                // observes the authored value (the Unity/Godot "apply overrides
                // before _ready" timing). No-op for backends without exposed vars.
                if (IScriptBackend* backend = runtime_.FindBackend(tracked.behaviour.backend); backend != nullptr) {
                    backend->ApplyExposedVariableOverrides(
                        tracked.entity, tracked.behaviour, scene.Entities().BehaviourVariableOverrides(tracked.entity));
                }
                ExecuteBehaviourPhase(scene, tracked.entity, tracked.behaviour, ScriptLifecycleEvent::Created, deltaSeconds);
                tracked.created = true;
            }
            if (!tracked.active) {
                ExecuteBehaviourPhase(scene, tracked.entity, tracked.behaviour, ScriptLifecycleEvent::Activated, deltaSeconds);
                ExecuteBehaviourPhase(scene, tracked.entity, tracked.behaviour, ScriptLifecycleEvent::Ready, deltaSeconds);
                tracked.active = true;
            }
            continue;
        }

        if (tracked.active) {
            ExecuteBehaviourPhase(scene, tracked.entity, tracked.behaviour, ScriptLifecycleEvent::Deactivated, deltaSeconds);
            tracked.active = false;
        }
        tracked.entity = current.entity;
        tracked.behaviour = current.behaviour;
    }

    // lifecycleRecords_ is an unordered_map: collect the departing records
    // first and dispatch Deactivated/Destroyed in the guaranteed execution
    // order instead of unordered_map's iteration order, which is not part of
    // the ordering contract behaviours may rely on. MakeKey() is pure, so
    // the key for erase can be recomputed after sorting instead of carrying
    // a parallel key alongside each record.
    std::vector<BehaviourLifecycleRecord> removed;
    for (const auto& [key, tracked] : lifecycleRecords_) {
        if (seen.contains(key)) {
            continue;
        }
        removed.push_back(tracked);
    }
    DispatchDeactivateAndDestroyInOrder(scene, removed, deltaSeconds);
    for (const BehaviourLifecycleRecord& tracked : removed) {
        static_cast<void>(lifecycleRecords_.erase(MakeKey(tracked.entity, tracked.behaviour)));
    }
}

void ScriptRuntimeSceneSystem::ShutdownTrackedBehaviours(kb::scene::Scene& scene, float deltaSeconds) {
    SyncBehaviourLifecycles(scene, deltaSeconds);

    std::vector<BehaviourLifecycleRecord> remaining;
    remaining.reserve(lifecycleRecords_.size());
    for (const auto& [key, tracked] : lifecycleRecords_) {
        (void)key;
        remaining.push_back(tracked);
    }
    DispatchDeactivateAndDestroyInOrder(scene, remaining, deltaSeconds);
    lifecycleRecords_.clear();
}

// Shared by SyncBehaviourLifecycles (departing records) and
// ShutdownTrackedBehaviours (all tracked records): sorts in the guaranteed
// execution order (see kb::scene::BehaviourExecutionOrderLess), then
// dispatches Deactivated to every still-active record and Destroyed to
// every still-created one. Kept as the single place that pairs "sort in
// execution order" with "tear down a batch of records", so the two callers
// cannot drift back into unordered dispatch the way they did before this
// was extracted.
void ScriptRuntimeSceneSystem::DispatchDeactivateAndDestroyInOrder(
    kb::scene::Scene& scene,
    std::vector<BehaviourLifecycleRecord>& records,
    float deltaSeconds) {
    std::ranges::sort(records, [](const BehaviourLifecycleRecord& lhs, const BehaviourLifecycleRecord& rhs) {
        return kb::scene::BehaviourExecutionOrderLess(lhs.entity, lhs.behaviour, rhs.entity, rhs.behaviour);
    });
    for (const BehaviourLifecycleRecord& tracked : records) {
        if (tracked.active) {
            ExecuteBehaviourPhase(scene, tracked.entity, tracked.behaviour, ScriptLifecycleEvent::Deactivated, deltaSeconds);
        }
        if (tracked.created) {
            ExecuteBehaviourPhase(scene, tracked.entity, tracked.behaviour, ScriptLifecycleEvent::Destroyed, deltaSeconds);
        }
    }
}

void ScriptRuntimeSceneSystem::ExecuteBehaviourPhase(
    kb::scene::Scene& scene,
    kb::scene::SceneEntity entity,
    const kb::scene::BehaviourComponent& behaviour,
    ScriptLifecycleEvent event,
    float deltaSeconds) {
    MergeResult(lastResult_, runtime_.ExecuteLifecycleForBehaviourAndDispatchEvents(scene, entity, behaviour, event, deltaSeconds));
}

std::vector<ScriptRuntimeSceneSystem::BehaviourLifecycleRecord> ScriptRuntimeSceneSystem::CollectBehaviourRecords(kb::scene::Scene& scene) const {
    std::vector<RawBehaviourRecord> rawRecords;
    BehaviourCollectContext context{
        .records = &rawRecords,
    };
    scene.Components().Behaviours().ForEach(&CollectBehaviour, &context);
    std::vector<BehaviourLifecycleRecord> records;
    records.reserve(rawRecords.size());
    for (const RawBehaviourRecord& raw : rawRecords) {
        records.push_back(BehaviourLifecycleRecord{
            .entity = raw.entity,
            .behaviour = raw.behaviour,
            .active = raw.behaviour.enabled,
            .created = false,
        });
    }
    std::ranges::sort(records, [](const BehaviourLifecycleRecord& lhs, const BehaviourLifecycleRecord& rhs) {
        return kb::scene::BehaviourExecutionOrderLess(lhs.entity, lhs.behaviour, rhs.entity, rhs.behaviour);
    });
    return records;
}

ScriptRuntimeSceneSystem::BehaviourLifecycleKey ScriptRuntimeSceneSystem::MakeKey(kb::scene::SceneEntity entity, const kb::scene::BehaviourComponent& behaviour) noexcept {
    return BehaviourLifecycleKey{
        .entityId = entity.Id(),
        .assetId = behaviour.behaviourAssetId,
        .backend = behaviour.backend,
    };
}

} // namespace kb::script
