#pragma once

#include "engine/library/EngineLibraryLifecycle.hpp"

#include <cstdint>

namespace kb::library {

// Structural World operations are synchronous. A kb::ecs::CommandBuffer (and
// the library CommandBatch wrapper) records changes until the caller invokes
// Playback/Flush; no lifecycle phase flushes a caller-owned buffer implicitly.
enum class CommandApplicationPoint : std::uint8_t {
    Immediate,
    DeferredUntilPlayback,
};

// Direct script-facing World operations are visible when the call returns in
// every lifecycle phase. Dispatch operates on a behaviour snapshot, so a newly
// spawned behaviour becomes readable immediately but joins lifecycle dispatch
// on the next frame-start synchronization.
[[nodiscard]] constexpr CommandApplicationPoint CommandApplicationPointFor(LifecycleEvent /*phase*/) noexcept {
    return CommandApplicationPoint::Immediate;
}

// LIB-128 production fixed-step contract:
//
//   SceneSystem::OnFrameStart
//   variable systems in SceneUpdatePhase::PreFixed (including input)
//   for each authoritative SceneRuntime fixed step:
//       Script FixedTick (SceneFixedUpdatePhase::PreSimulation)
//       caller-owned CommandBatch::Flush, if requested by the script
//       transform hierarchy synchronization
//       physics simulation + write-back (Simulation)
//       post-simulation systems
//   variable systems in SceneUpdatePhase::PostFixed (script Tick/LateTick)
//
// ScriptRuntimeSceneSystem::RequiresFixedStep() is true and its installed
// runtime does not use its private ExecuteFrame accumulator. Its host settings
// configure SceneRuntimeFixedStepSettings, which is the single production
// accumulator consumed by both FixedTick and physics. Therefore the number and
// delta of FixedTick calls exactly match physics substeps.
//
// Direct World commands are immediate. CommandBatch commands stay deferred
// until Flush(), but a Flush performed inside FixedTick completes before the
// hierarchy sync and matching physics simulation. Physics commands such as
// SetVelocity also reach the live backend synchronously during FixedTick and
// are consumed by that same simulation step.
//
// Physics contact events are queued during Simulation and drained by the
// post-fixed script update before Tick, so collision callbacks and Tick observe
// the result of the same SceneRuntime::Update call.
[[nodiscard]] constexpr bool PhysicsSimulationSeesSameFrameFixedTick() noexcept {
    return true;
}

} // namespace kb::library
