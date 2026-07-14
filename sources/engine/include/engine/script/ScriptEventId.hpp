#pragma once

#include <cstdint>
#include <string_view>

namespace kb::script {

// LIB-104: the typed dispatch key that replaces raw ScriptEvent::name string
// comparisons on the event dispatch hot path. ScriptRuntime::DispatchEvent
// (the single choke point every event kind flows through — scene lifecycle/
// LIB-073, TimerFired/LIB-095, TaskCompleted+TaskFailed/LIB-097-098, script
// Emit/EmitTo, Visual Graph EmitEvent, confirmed by LIB-103's research)
// visits every enabled behaviour in the scene once per dispatched event.
// Before this type existed, NativeScriptBackend::ExecuteEvent rebuilt a
// heap-allocated "<assetId>:<eventName>" string on EVERY one of those
// visits (NativeScriptBackend.cpp's old EventKey helper) — O(behaviour
// count) allocations per single event dispatch. EventId is computed ONCE
// per dispatch (ScriptRuntime::DispatchEvent, from ScriptEvent::Id()) and
// carried down through IScriptBackend::ExecuteEvent's eventId parameter, so
// NativeScriptBackend's callback lookup is a plain POD (assetId, EventId)
// hash — zero allocation, zero string comparison, regardless of how many
// behaviours receive the event.
//
// ScriptEvent::name remains the single source of truth (authoring surface
// for Lua/Visual Graph/diagnostics, and what Emit/EmitTo/native plugins
// register against) — EventId is always derived from it, never stored as
// an independent field that could drift out of sync.
using EventId = std::uint64_t;

// Same FNV-1a 64-bit algorithm as kb::library::ComputeLibraryFunctionId/
// ComputeLibraryComponentId, kept as an independent implementation here:
// kb::script must not depend on kb::library (the dependency runs the other
// way — kb::library wraps kb::script), so the two layers each carry their
// own copy of this well-established stable-hash technique rather than
// sharing one.
[[nodiscard]] constexpr EventId ComputeEventId(std::string_view name) noexcept {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const unsigned char byte : name) {
        hash ^= byte;
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

} // namespace kb::script
