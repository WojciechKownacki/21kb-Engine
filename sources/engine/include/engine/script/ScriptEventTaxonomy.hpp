#pragma once

#include "engine/script/ScriptEvent.hpp"

namespace kb::script {

// LIB-103: the canonical, named way to classify a ScriptEvent into one of
// the two delivery categories that actually exist today — see ScriptEvent's
// own doc comment (ScriptEvent.hpp) for the full taxonomy, including why
// "inter-system" is deliberately NOT a third case here (it has no
// implementation to classify yet — LIB-105+). Mutually exclusive and
// exhaustive over ScriptEvent's real state: exactly one of these is true
// for any event, always (both are trivial negations of target.IsValid()).

// ENTITY-LOCAL: delivered to only the behaviour(s) on event.target.
[[nodiscard]] inline bool IsEntityLocalEvent(const ScriptEvent& event) noexcept {
    return event.target.IsValid();
}

// WORLD: broadcast to every enabled behaviour in the scene.
[[nodiscard]] inline bool IsWorldEvent(const ScriptEvent& event) noexcept {
    return !event.target.IsValid();
}

} // namespace kb::script
