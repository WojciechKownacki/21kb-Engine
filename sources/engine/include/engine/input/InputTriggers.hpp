#pragma once

#include "engine/input/InputTriggerDesc.hpp"

#include <cstdint>

namespace kb::input {

// Outcome of a single trigger for the current frame, mirroring Unreal's
// ETriggerState progression.
enum class TriggerState : std::uint8_t {
    None,     // Not actuated / not satisfied.
    Ongoing,  // Actuated and progressing toward firing (e.g. Hold counting up).
    Triggered // Fires this frame.
};

// Per-trigger persistent state carried across frames.
struct TriggerRuntimeState {
    TriggerState state = TriggerState::None;
    float heldSeconds = 0.0F;   // Time the value has been continuously actuated.
    float pulseAccum = 0.0F;    // Time since the last pulse fired.
    std::uint32_t pulseCount = 0U; // Pulses fired this actuation (for the limit).
    bool prevActuated = false;  // Whether the value was actuated last frame.
    bool firedThisActuation = false; // For one-shot Hold.
};

// Evaluates one trigger. `magnitude` is the modified value's magnitude.
// `chordSatisfied` is only consulted by Chorded triggers (the referenced action's
// triggered state, resolved by the subsystem).
[[nodiscard]] TriggerState EvaluateTrigger(const InputTriggerDesc& trigger, TriggerRuntimeState& state,
                                           float magnitude, float dt, bool chordSatisfied);

// True for triggers that gate (must be satisfied) rather than drive firing.
[[nodiscard]] constexpr bool IsImplicitTrigger(InputTriggerType type) noexcept {
    return type == InputTriggerType::Chorded;
}

// Combines two trigger states: Triggered dominates Ongoing dominates None.
[[nodiscard]] constexpr TriggerState MaxState(TriggerState lhs, TriggerState rhs) noexcept {
    return static_cast<std::uint8_t>(lhs) >= static_cast<std::uint8_t>(rhs) ? lhs : rhs;
}

} // namespace kb::input
