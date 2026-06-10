#include "engine/input/InputTriggers.hpp"

#include <algorithm>

namespace kb::input {
namespace {

[[nodiscard]] float ActuationThreshold(const InputTriggerDesc& trigger) noexcept {
    return trigger.params[0] > 0.0F ? trigger.params[0] : 0.5F;
}

TriggerState EvaluateDown(bool actuated) {
    return actuated ? TriggerState::Triggered : TriggerState::None;
}

TriggerState EvaluatePressed(bool actuated, TriggerRuntimeState& state) {
    return (actuated && !state.prevActuated) ? TriggerState::Triggered : TriggerState::None;
}

TriggerState EvaluateReleased(bool actuated, TriggerRuntimeState& state) {
    if (actuated) {
        return TriggerState::Ongoing;
    }
    return state.prevActuated ? TriggerState::Triggered : TriggerState::None;
}

TriggerState EvaluateHold(const InputTriggerDesc& trigger, TriggerRuntimeState& state, bool actuated, float dt) {
    const float holdTime = trigger.params[1] > 0.0F ? trigger.params[1] : 1.0F;
    const bool oneShot = trigger.params[2] > 0.5F;
    if (!actuated) {
        state.heldSeconds = 0.0F;
        state.firedThisActuation = false;
        return TriggerState::None;
    }
    state.heldSeconds += dt;
    if (state.heldSeconds >= holdTime) {
        if (oneShot && state.firedThisActuation) {
            return TriggerState::Ongoing;
        }
        state.firedThisActuation = true;
        return TriggerState::Triggered;
    }
    return TriggerState::Ongoing;
}

TriggerState EvaluateHoldAndRelease(const InputTriggerDesc& trigger, TriggerRuntimeState& state, bool actuated,
                                    float dt) {
    const float holdTime = trigger.params[1] > 0.0F ? trigger.params[1] : 1.0F;
    if (actuated) {
        state.heldSeconds += dt;
        return TriggerState::Ongoing;
    }
    // Released this frame: fire if we held long enough.
    if (state.prevActuated) {
        const bool longEnough = state.heldSeconds >= holdTime;
        state.heldSeconds = 0.0F;
        return longEnough ? TriggerState::Triggered : TriggerState::None;
    }
    state.heldSeconds = 0.0F;
    return TriggerState::None;
}

TriggerState EvaluateTap(const InputTriggerDesc& trigger, TriggerRuntimeState& state, bool actuated, float dt) {
    const float tapTime = trigger.params[1] > 0.0F ? trigger.params[1] : 0.2F;
    if (actuated) {
        state.heldSeconds += dt;
        return TriggerState::Ongoing;
    }
    if (state.prevActuated) {
        const bool quickEnough = state.heldSeconds <= tapTime;
        state.heldSeconds = 0.0F;
        return quickEnough ? TriggerState::Triggered : TriggerState::None;
    }
    state.heldSeconds = 0.0F;
    return TriggerState::None;
}

TriggerState EvaluatePulse(const InputTriggerDesc& trigger, TriggerRuntimeState& state, bool actuated, float dt) {
    const float interval = trigger.params[1] > 0.0F ? trigger.params[1] : 1.0F;
    const bool triggerOnStart = trigger.params[2] > 0.5F;
    const auto limit = static_cast<std::uint32_t>(std::max(trigger.params[3], 0.0F));
    if (!actuated) {
        state.pulseAccum = 0.0F;
        state.pulseCount = 0U;
        return TriggerState::None;
    }

    bool fire = false;
    if (!state.prevActuated) {
        // First frame of actuation.
        state.pulseAccum = 0.0F;
        if (triggerOnStart) {
            fire = true;
        }
    } else {
        state.pulseAccum += dt;
        if (state.pulseAccum >= interval) {
            state.pulseAccum -= interval;
            fire = true;
        }
    }

    if (fire) {
        if (limit != 0U && state.pulseCount >= limit) {
            return TriggerState::Ongoing;
        }
        ++state.pulseCount;
        return TriggerState::Triggered;
    }
    return TriggerState::Ongoing;
}

} // namespace

TriggerState EvaluateTrigger(const InputTriggerDesc& trigger, TriggerRuntimeState& state, float magnitude,
                             float dt, bool chordSatisfied) {
    const bool actuated = magnitude >= ActuationThreshold(trigger);

    TriggerState result = TriggerState::None;
    switch (trigger.type) {
        case InputTriggerType::Down:
            result = EvaluateDown(actuated);
            break;
        case InputTriggerType::Pressed:
            result = EvaluatePressed(actuated, state);
            break;
        case InputTriggerType::Released:
            result = EvaluateReleased(actuated, state);
            break;
        case InputTriggerType::Hold:
            result = EvaluateHold(trigger, state, actuated, dt);
            break;
        case InputTriggerType::HoldAndRelease:
            result = EvaluateHoldAndRelease(trigger, state, actuated, dt);
            break;
        case InputTriggerType::Tap:
            result = EvaluateTap(trigger, state, actuated, dt);
            break;
        case InputTriggerType::Pulse:
            result = EvaluatePulse(trigger, state, actuated, dt);
            break;
        case InputTriggerType::Chorded:
            result = chordSatisfied ? TriggerState::Triggered : TriggerState::None;
            break;
    }

    state.prevActuated = actuated;
    state.state = result;
    return result;
}

} // namespace kb::input
