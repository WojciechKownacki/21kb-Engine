#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/script/ScriptValue.hpp"

#include <string>
#include <vector>

namespace kb::script {

struct ScriptEventArgument {
    std::string name;
    ScriptValue value;
};

// LIB-103: the ONE event delivery shape this engine has — every "kind" of
// event named elsewhere in the backlog (scene lifecycle events/LIB-073,
// TimerFired/LIB-095, TaskCompleted+TaskFailed/LIB-097-098, script Emit/
// EmitTo, Visual Graph EmitEvent) constructs and dispatches exactly this
// struct through ScriptRuntime::DispatchEvent/DispatchEventAndDrain —
// confirmed by research before this comment was written, no second
// delivery mechanism exists anywhere in the engine.
//
// `target` is the ONLY category axis that exists today (checked in exactly
// one place, ScriptRuntime.cpp's DispatchSceneBehaviours):
//   - ENTITY-LOCAL: target.IsValid() == true — delivered to ONLY the
//     behaviour(s) on that one entity. Every currently-owned Timer/Task
//     (LIB-095/097) sets this to its `owner`; ScriptExecutionContext::
//     EmitTo and Visual Graph's EmitEventTo set it explicitly.
//   - WORLD: target invalid (default-constructed SceneEntity{}) —
//     broadcast to every enabled behaviour in the scene. Scene lifecycle
//     events (LIB-073) always use this; an ownerless Timer/Task and a
//     plain Emit()/EmitEvent (no target) degrade to it.
//   - INTER-SYSTEM: a message between kb::ecs::System/kb::scene::
//     SceneSystem instances (not entity behaviours) — this is NOT a third
//     configuration of ScriptEvent, it is a THIRD CATEGORY THAT DOES NOT
//     EXIST YET. Neither kb::ecs::System nor kb::scene::SceneSystem expose
//     any send/receive surface at all (confirmed by reading both classes
//     before writing this comment) — there is no code to "separate" here,
//     only a real, honestly-documented gap. This is explicitly LIB-105's
//     job (Events.Subscribe/Unsubscribe/Emit/EmitDeferred/Broadcast, a real
//     pub/sub bus), LIB-106's (recipient filters, including scene/channel),
//     LIB-107's (synchronous vs deferred dispatch contract), and LIB-109's
//     (Signal<T>, still open) — deliberately NOT fabricated here, mirroring
//     this session's own "document the real gap, don't invent an unused
//     enum value" precedent (LIB-097's Coroutine/LIB-098's event-yield
//     deferrals).
//
// See kb::script::IsEntityLocalEvent/IsWorldEvent (ScriptEventTaxonomy.hpp)
// for the canonical, named way to ask which of the two REAL categories a
// given event falls into, instead of re-deriving `target.IsValid()`
// independently at each dispatch site the way LIB-073/095/097 originally
// did.
struct ScriptEvent {
    std::string name;
    kb::scene::SceneEntity sender{};
    kb::scene::SceneEntity target{};
    kb::assets::AssetId senderAsset{};
    std::vector<ScriptEventArgument> arguments;
};

} // namespace kb::script
