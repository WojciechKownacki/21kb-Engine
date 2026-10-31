#include "engine/input/InputSubsystem.hpp"

#include "engine/input/InputMappingEvaluator.hpp"

#include <cmath>
#include <string>
#include <utility>

namespace kb::input {

void InputSubsystem::SetResolvers(ActionResolver actionResolver, ContextResolver contextResolver) {
    stack_.SetResolvers(std::move(actionResolver), std::move(contextResolver));
}

bool InputSubsystem::AddMappingContext(std::uint64_t contextId, std::int32_t priority) {
    return stack_.Add(contextId, priority);
}

void InputSubsystem::RemoveMappingContext(std::uint64_t contextId) {
    stack_.Remove(contextId);
}

void InputSubsystem::ClearMappingContexts() noexcept {
    stack_.Clear();
}

bool InputSubsystem::HasMappingContext(std::uint64_t contextId) const noexcept {
    return stack_.Has(contextId);
}

InputRuntimeRebindResult InputSubsystem::Rebind(
    std::uint64_t contextId, std::uint64_t bindingId, InputKey newKey,
    std::uint8_t gamepadIndex, bool allowConflict) {
    return stack_.Rebind(contextId, bindingId, newKey, gamepadIndex, allowConflict);
}

bool InputSubsystem::SetRebindProfile(
    std::uint64_t contextId, std::span<const InputRebindOverride> overrides) {
    return stack_.SetRebindProfile(contextId, overrides);
}

std::span<const InputRebindOverride> InputSubsystem::RebindProfile(
    std::uint64_t contextId) const noexcept {
    return stack_.RebindProfile(contextId);
}

void InputSubsystem::Evaluate(float deltaSeconds) {
    EvaluateWithDeviceState(deviceState_, deltaSeconds);
}

void InputSubsystem::EvaluateWithDeviceState(const InputDeviceState& physical, float deltaSeconds) {
    gameplayDevice_ = physical;
    gameplayInputFiltered_ = gameplayInputBlocked_;
    for (std::size_t index = 0U; index < InputDeviceState::kKeyCount; ++index) {
        const auto key = static_cast<InputKey>(index);
        if (key == InputKey::TouchDown) continue;
        const std::uint8_t slots = DeviceKindOf(key) == InputDeviceKind::Gamepad ? InputDeviceState::kMaxGamepads : 1U;
        for (std::uint8_t pad = 0U; pad < slots; ++pad) {
            const bool relative = DeviceKindOf(key) == InputDeviceKind::Mouse && IsAnalogKey(key);
            const float neutral = DeviceKindOf(key) == InputDeviceKind::Gamepad && IsAnalogKey(key) ? 0.2F : 0.0F;
            const bool held = !relative && std::abs(physical.GetValue(key, pad)) > neutral;
            const bool suppress = gameplayInputBlocked_ || suppressedControls_.IsKeyDown(key, pad);
            suppressedControls_.SetKeyDown(key, suppress && held, pad);
            if (suppress) {
                gameplayDevice_.SetKeyDown(key, false, pad);
                gameplayDevice_.SetAnalog(key, 0.0F, pad);
                gameplayInputFiltered_ = true;
            }
        }
    }
    std::array<InputTouchPoint, InputDeviceState::kMaxTouchPoints> touches{};
    std::array<std::uint32_t, InputDeviceState::kMaxTouchPoints> heldTouches{};
    std::size_t touchCount = 0U, heldCount = 0U;
    for (const auto& touch : physical.TouchPoints()) {
        const bool suppressed = gameplayInputBlocked_ ||
            std::find(suppressedTouches_.begin(), suppressedTouches_.end(), touch.id) != suppressedTouches_.end();
        if (suppressed) {
            gameplayInputFiltered_ = true;
            if (IsTouchActive(touch.phase)) heldTouches[heldCount++] = touch.id;
        } else touches[touchCount++] = touch;
    }
    suppressedTouches_.assign(heldTouches.begin(), heldTouches.begin() + static_cast<std::ptrdiff_t>(heldCount));
    gameplayDevice_.SetTouchPoints(std::span{touches}.first(touchCount));
    if (gameplayInputBlocked_) gameplayDevice_.ClearTextInput();
    const InputDeviceState& device = gameplayDevice_;
    // Snapshot last frame's combined states (used for edge events and chords),
    // then rebuild this frame's action states from scratch.
    previousCombined_.clear();
    for (const auto& [name, state] : actionStates_) {
        previousCombined_[name] = state.combined;
    }
    actionStates_.clear();
    frameEvents_.clear();

    if (gameplayInputBlocked_) {
        // A modal cancels held gameplay, rather than synthesizing a key release
        // that could fire a Tap/Released trigger underneath the dialog.
        for (auto& context : stack_.Active()) {
            for (auto& mapping : context.mappings) {
                mapping.modifierState = {};
                for (auto& trigger : mapping.triggerStates) trigger = {};
            }
            for (auto& composite : context.composites) {
                composite.modifierState = {};
                for (auto& trigger : composite.triggerStates) trigger = {};
            }
        }
        for (const auto& [name, previous] : previousCombined_) {
            if (previous == TriggerState::None) continue;
            actionStates_[name].canceled = true;
            frameEvents_.push_back({name, InputActionPhase::Canceled, InputValue{}});
        }
        return;
    }

    // Keyed by (key, gamepadIndex) - the SAME InputKey value on two different
    // gamepads is a different physical control, so consumption must not alias
    // across controllers.
    std::unordered_map<std::uint32_t, bool> consumedKeys;
    const auto consumeKey = [](InputKey key, std::uint8_t gamepadIndex) noexcept -> std::uint32_t {
        return (static_cast<std::uint32_t>(gamepadIndex) << 16U) | static_cast<std::uint16_t>(key);
    };
    const InputMappingEvaluator evaluator;
    const auto chordSatisfied = [this](std::string_view action) {
        const auto found = previousCombined_.find(std::string{action});
        return found != previousCombined_.end() && found->second == TriggerState::Triggered;
    };

    for (ActiveMappingContext& context : stack_.Active()) {
        for (ResolvedMapping& mapping : context.mappings) {
            const std::uint32_t keyIndex = consumeKey(mapping.key, mapping.gamepadIndex);
            if (consumedKeys[keyIndex]) {
                continue; // A higher-priority context already claimed this key.
            }

            const MappingEvaluationInput evalInput{
                .valueType = mapping.valueType,
                .raw = InputValue{.x = device.GetValue(mapping.key, mapping.gamepadIndex) * mapping.scale, .type = mapping.valueType},
                .modifiers = mapping.modifiers,
                .triggers = mapping.triggers,
                .chordActionNames = mapping.chordActionNames,
            };
            const MappingEvaluationResult result = evaluator.Evaluate(
                evalInput, deltaSeconds, mapping.modifierState, mapping.triggerStates, chordSatisfied);

            // Axes (Axis1D/2D/3D) contribute their continuous value every frame so
            // analog sticks and sub-threshold movement register; buttons (Bool)
            // only contribute while a trigger is firing.
            const bool isAxis = mapping.valueType != InputActionValueType::Bool;
            if (!isAxis && result.state == TriggerState::None) {
                continue;
            }

            InputActionState& actionState = actionStates_[mapping.actionName];
            actionState.value.type = mapping.valueType;
            actionState.value.x += result.value.x;
            actionState.value.y += result.value.y;
            actionState.value.z += result.value.z;
            actionState.combined = MaxState(actionState.combined, result.state);

            if (result.state == TriggerState::Triggered && mapping.consumeInput) {
                consumedKeys[keyIndex] = true;
            }
        }

        for (ResolvedComposite& composite : context.composites) {
            // Skip a slot whose key a higher-priority context already claimed, but
            // still combine whichever slots remain free (e.g. W free, S consumed).
            InputValue raw{.type = composite.valueType};
            bool anyFree = false;
            for (const InputCompositeSlot& slot : composite.slots) {
                if (consumedKeys[consumeKey(slot.key, slot.gamepadIndex)]) {
                    continue;
                }
                anyFree = true;
                const float contribution = device.GetValue(slot.key, slot.gamepadIndex) * slot.scale;
                switch (slot.axis) {
                    case 0U:
                        raw.x += contribution;
                        break;
                    case 1U:
                        raw.y += contribution;
                        break;
                    default:
                        raw.z += contribution;
                        break;
                }
            }
            if (!anyFree) {
                continue;
            }

            const MappingEvaluationInput evalInput{
                .valueType = composite.valueType,
                .raw = raw,
                .modifiers = composite.modifiers,
                .triggers = composite.triggers,
                .chordActionNames = composite.chordActionNames,
            };
            const MappingEvaluationResult result = evaluator.Evaluate(
                evalInput, deltaSeconds, composite.modifierState, composite.triggerStates, chordSatisfied);

            const bool isAxis = composite.valueType != InputActionValueType::Bool;
            if (!isAxis && result.state == TriggerState::None) {
                continue;
            }

            InputActionState& actionState = actionStates_[composite.actionName];
            actionState.value.type = composite.valueType;
            actionState.value.x += result.value.x;
            actionState.value.y += result.value.y;
            actionState.value.z += result.value.z;
            actionState.combined = MaxState(actionState.combined, result.state);

            if (result.state == TriggerState::Triggered && composite.consumeInput) {
                for (const InputCompositeSlot& slot : composite.slots) {
                    consumedKeys[consumeKey(slot.key, slot.gamepadIndex)] = true;
                }
            }
        }
    }

    // Resolve per-frame edge events from previous vs current combined state.
    for (auto& [name, state] : actionStates_) {
        state.value.ClampToType();
        const auto prevIt = previousCombined_.find(name);
        const TriggerState prev = prevIt != previousCombined_.end() ? prevIt->second : TriggerState::None;

        state.started = prev == TriggerState::None && state.combined != TriggerState::None;
        state.triggered = state.combined == TriggerState::Triggered;
        state.completed = prev == TriggerState::Triggered && state.combined == TriggerState::None;
        state.canceled = prev == TriggerState::Ongoing && state.combined == TriggerState::None;
        if (state.completed) {
            frameEvents_.push_back({name, InputActionPhase::Completed, state.value});
        } else if (state.canceled) {
            frameEvents_.push_back({name, InputActionPhase::Canceled, state.value});
        }

        if (state.started) {
            frameEvents_.push_back({name, InputActionPhase::Started, state.value});
        }
        if (state.triggered) {
            frameEvents_.push_back({name, InputActionPhase::Triggered, state.value});
        }
    }

    // Actions that dropped out entirely this frame emit Completed/Canceled.
    for (const auto& [name, prev] : previousCombined_) {
        if (prev == TriggerState::None || actionStates_.contains(name)) {
            continue;
        }
        InputActionState ended;
        ended.combined = TriggerState::None;
        if (prev == TriggerState::Triggered) {
            ended.completed = true;
            frameEvents_.push_back({name, InputActionPhase::Completed, InputValue{}});
        } else {
            ended.canceled = true;
            frameEvents_.push_back({name, InputActionPhase::Canceled, InputValue{}});
        }
        actionStates_[name] = ended;
    }
}

const InputActionState* InputSubsystem::FindState(std::string_view action) const {
    const auto found = actionStates_.find(std::string{action});
    return found != actionStates_.end() ? &found->second : nullptr;
}

bool InputSubsystem::IsActionPressed(std::string_view action) const {
    const InputActionState* state = FindState(action);
    return state != nullptr && state->combined == TriggerState::Triggered;
}

InputValue InputSubsystem::GetActionValue(std::string_view action) const {
    const InputActionState* state = FindState(action);
    return state != nullptr ? state->value : InputValue{};
}

bool InputSubsystem::WasActionStarted(std::string_view action) const {
    const InputActionState* state = FindState(action);
    return state != nullptr && state->started;
}

bool InputSubsystem::WasActionTriggered(std::string_view action) const {
    const InputActionState* state = FindState(action);
    return state != nullptr && state->triggered;
}

bool InputSubsystem::WasActionCompleted(std::string_view action) const {
    const InputActionState* state = FindState(action);
    return state != nullptr && state->completed;
}

bool InputSubsystem::WasActionReleased(std::string_view action) const {
    const InputActionState* state = FindState(action);
    return state != nullptr && (state->completed || state->canceled);
}

} // namespace kb::input
