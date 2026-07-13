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

} // namespace kb::library
