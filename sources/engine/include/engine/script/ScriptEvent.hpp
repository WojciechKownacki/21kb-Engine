#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/script/ScriptEventId.hpp"
#include "engine/script/ScriptValue.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace kb::script {

// LIB-108: "event payload value types" — a ScriptEventArgument's `value` is
// a plain kb::script::ScriptValue, the SAME closed scalar type set every
// other script boundary in this engine already uses (function arguments,
// shared state, component properties — LIB-032/041). No separate "event
// value type" system exists or is needed: kb::library::LibraryTypeDesc/
// DescribeType (EngineLibraryTypeDesc.hpp) already documents, for every
// ScriptValueType including every one an event payload can carry, its
// canonical name, Visual Graph pin type, Lua marshalling type, and default
// value — reused here, not duplicated.
struct ScriptEventArgument {
    std::string name;
    ScriptValue value;
};

// LIB-108: "limity rozmiaru" (size limits) — the largest number of
// arguments a single ScriptEvent may carry, enforced at every real dispatch
// entry point (ScriptEventBus::Emit, ScriptRuntime::DispatchEvent — between
// them these cover EVERY event delivery path in the engine, confirmed by
// LIB-103's "exactly one delivery mechanism" research: Emit/Broadcast/
// EmitDeferred, ScriptExecutionContext::Emit/EmitTo, Visual Graph
// EmitEvent/EmitEventTo, and every engine-emitted event — TimerFired/
// TaskCompleted/TaskFailed/scene lifecycle). Mirrors kb::library::
// kDefaultLibraryInputLimits.maxEventPayloadArguments (EngineLibraryInputLimits.
// hpp) — SAME numeric value, duplicated as a plain local constant here
// rather than #include'd, because kb::script must never depend on
// kb::library (kb::library wraps kb::script, never the reverse — the same
// direction constraint SceneTimerService.cpp's kMaxLiveTimers already
// documents for kb::scene). Was reserved/unenforced until now (LIB-037's
// own note: enforcing it needed a real diagnostic channel, which Emit/
// EmitTo's void return did not have — LIB-105 gave ScriptEventBus::Emit one
// (ScriptEventDeliveryResult::errors), and ScriptRuntime::DispatchEvent
// already had one (ScriptRuntimeExecutionResult::diagnostics) — so this is
// no longer a fabricated check, it can now fail HONESTLY instead of
// silently truncating or silently proceeding).
inline constexpr std::size_t kMaxScriptEventArguments = 32U;

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

    // LIB-104: the typed dispatch key derived from `name` — see
    // ScriptEventId.hpp for why this exists and how it is used on the hot
    // dispatch path. Always computed from `name`, never cached as a
    // separate field, so the two can never drift out of sync.
    [[nodiscard]] EventId Id() const noexcept {
        return ComputeEventId(name);
    }
};

} // namespace kb::script
