#pragma once

#include "engine/library/EngineLibraryLifecycle.hpp"

#include <cstdint>

namespace kb::library {

// When a structural ECS command (Spawn, Destroy, Add/RemoveComponent, ...)
// issued from script code becomes visible to the rest of the world. Two
// mechanisms exist in the engine today and kb::library does not merge them
// into a third: kb::ecs::World's structural mutators (CreateEntity,
// DestroyEntity, SetComponent, AddTag, ...) apply synchronously, guarded
// only against being called during an open Query iteration
// (kb::ecs::StructuralChangeValidator throws in that case); kb::ecs::CommandBuffer
// applies nothing until an explicit Playback(World&) call, which no
// lifecycle phase performs automatically.
enum class CommandApplicationPoint : std::uint8_t {
    // Applied synchronously: the call that issued the command has fully
    // taken effect before it returns, so anything reading the world
    // afterwards (including a later-dispatched behaviour in the same
    // phase) observes the change immediately.
    Immediate,
    // Only recorded; has no effect on the world until something calls
    // kb::ecs::CommandBuffer::Playback(world) explicitly. No kb::library
    // lifecycle phase performs this flush on a script's behalf.
    DeferredUntilPlayback,
};

// The point of application for every lifecycle phase today: Immediate.
// kb::library does not introduce a phase-scoped ECS command queue —
// World.Spawn/World.Destroy (ScriptWorldApi) and every other structural
// script call go straight through kb::ecs::World and are live the moment
// the call returns, in Created, Activated, Ready, FixedTick, Tick,
// LateTick, BeforeRender, AfterRender, Deactivated and Destroyed alike.
// This is safe because ScriptRuntime::DispatchSceneBehaviours and
// ScriptRuntimeSceneSystem::SyncBehaviourLifecycles always collect the set
// of behaviours to run into a snapshot (via World::ForEach, under its
// iteration guard) *before* invoking any script code, and only dispatch
// once that guard is released — so a script's own structural mutation
// never runs while a Query/ForEach of the same store is open.
//
// Two consequences a script author must know, because they follow from
// "immediate" rather than "batched at phase end":
//  - An entity spawned during phase P is immediately live and readable
//    (World.Exists, queries, component reads) by any behaviour dispatched
//    later in that same phase P, because dispatch order walks a snapshot
//    but reads go through the live world.
//  - That new entity is NOT part of phase P's already-collected dispatch
//    snapshot, so it does not receive phase P's event in the same call,
//    and it does not receive Created/Activated/Ready until the next call
//    that runs ScriptRuntimeSceneSystem::SyncBehaviourLifecycles (in
//    practice: the next ExecuteFrame/ExecuteStartup call, i.e. the next
//    frame — SyncBehaviourLifecycles runs once per such call, before any
//    FixedTick/Tick/LateTick/BeforeRender/AfterRender dispatch in it).
//
// A script (or kb::library module) that must batch structural changes
// instead of applying them immediately — e.g. to record them while a Query
// loop is open — can use kb::ecs::CommandBuffer directly and call
// Playback(world) at a point of its own choosing; kb::library exposes no
// wrapper for that today (see LIB-080 in others/Engine21kbLibrary.md).
[[nodiscard]] constexpr CommandApplicationPoint CommandApplicationPointFor(LifecycleEvent /*phase*/) noexcept {
    return CommandApplicationPoint::Immediate;
}

// LIB-128: WHEN a live physics simulation (kb::scene::IPhysicsBackend,
// JoltPhysicsSceneSystem::OnFixedUpdate in a real project) executes
// relative to LifecycleEvent::FixedTick, within one
// kb::scene::Scene::Runtime().Update() call.
//
// kb::scene::SceneRuntimeService::Update (SceneRuntime.cpp) calls, in this
// order, EVERY call:
//   1. state.sceneSystemScheduler.Update(...) - every registered
//      kb::scene::SceneSystem's OnUpdate(), once. This is where
//      kb::script::ScriptRuntimeSceneSystem::OnUpdate runs, which itself
//      drives its OWN accumulator, dispatching FixedTick zero or more
//      times, then Tick/LateTick/BeforeRender/AfterRender once.
//   2. The scene's OWN fixed-step accumulator
//      (kb::scene::SceneRuntimeFixedStepSettings — a SEPARATE setting from
//      (1)'s) may then run state.sceneSystemScheduler.FixedUpdate(...)
//      zero or more times — this is where a RequiresFixedStep()==true
//      system (JoltPhysicsSceneSystem in a real project) executes.
//
// Consequence: within one Update() call, every dispatched FixedTick (and
// Tick) has ALREADY happened by the time physics simulates for that same
// call — a script's FixedTick/Tick body observes the PREVIOUS Update()
// call's physics results, never the current call's. The reverse is NOT
// true: a fixed-step system DOES see a same-call, earlier-running
// variable-update system's change (SceneSystemTransformSyncTests.cpp's
// RunTransformSyncContractFixedStepGetsFreshDataAutomaticallyTest), because
// SynchronizeTransformHierarchy runs again immediately before the
// fixed-step loop. This asymmetry — proved empirically by
// RunFixedTickSeesPreviousFramePhysicsResultTest — is exactly why LIB-014's
// Projectile template retries Physics.SetVelocity every Tick instead of
// calling it once in Ready: a freshly-spawned entity's live physics body
// is not created until ITS first fixed step, which cannot have run yet by
// the time the SAME call's Ready/FixedTick/Tick already did.
//
// Two INDEPENDENT fixed-step accumulators exist side by side; nothing in
// kb::library keeps them synchronized:
//   - kb::scene::SceneRuntimeFixedStepSettings::fixedDeltaSeconds
//     (scene.Runtime().SetFixedStepSettings) drives the accumulator that
//     steps physics.
//   - kb::script::ScriptRuntimeFrameSettings::fixedDeltaSeconds
//     (ScriptRuntimeHostOptions::frameSettings, or
//     ScriptRuntimeSceneSystem::SetFrameSettings directly) drives
//     ScriptRuntimeSceneSystem's own, separate accumulator that dispatches
//     FixedTick.
// A project that wants FixedTick and physics stepping at the same rate
// must set BOTH to the same value; kb::library does not do this on a
// caller's behalf — proved genuinely independent, not silently unified, by
// RunFixedTickAndPhysicsAccumulatorsAreIndependentTest (configures them to
// different rates and watches the step counts diverge over real Update()
// calls).
//
// Command buffer relationship: physics's own ECS writes (WriteBack copying
// live simulation state into TransformComponent/RigidbodyComponent) are
// CommandApplicationPoint::Immediate, the SAME as every phase
// CommandApplicationPointFor already returns Immediate for — no deferred
// kb::ecs::CommandBuffer is involved; a script reading a Rigidbody's
// Transform right after a fixed step observes the just-written value
// synchronously, through the same "Immediate" contract documented above.
//
// Deliberately NOT changed by this task: the Update()-before-FixedUpdate
// order itself. An earlier attempt to reorder it (physics-before-script,
// matching Unity/Unreal's own FixedUpdate-before-Update convention) broke
// an existing, intentional, already-tested contract in the OTHER direction
// (RunTransformSyncContractFixedStepGetsFreshDataAutomaticallyTest, which
// depends on variable-update running first so physics can see its result
// the same call) — the two directions cannot both be "same-call fresh"
// simultaneously with a single Update-then-FixedUpdate pass per frame.
// Reconciling that would be a much larger redesign (e.g. running the
// fixed-step loop both before AND after the variable pass, or merging
// ScriptRuntimeSceneSystem's FixedTick dispatch into a real
// OnFixedUpdate override so it shares physics's own accumulator) than
// "define the relationship" calls for; the asymmetry is instead
// documented and locked in by test here, honestly, rather than
// papered over.
[[nodiscard]] constexpr bool PhysicsSimulationSeesSameFrameFixedTick() noexcept {
    return false;
}

} // namespace kb::library
