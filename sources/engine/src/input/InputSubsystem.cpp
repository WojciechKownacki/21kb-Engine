#include "engine/input/InputSubsystem.hpp"

#include "engine/input/InputMappingEvaluator.hpp"

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

void InputSubsystem::Evaluate(float deltaSeconds) {
    // Snapshot last frame's combined states (used for edge events and chords),
    // then rebuild this frame's action states from scratch.
    previousCombined_.clear();
    for (const auto& [name, state] : actionStates_) {
        previousCombined_[name] = state.combined;
    }
    actionStates_.clear();
    frameEvents_.clear();

    std::unordered_map<std::uint16_t, bool> consumedKeys; // key -> consumed by a higher context
    const InputMappingEvaluator evaluator;
    const auto chordSatisfied = [this](std::string_view action) {
        const auto found = previousCombined_.find(std::string{action});
        return found != previousCombined_.end() && found->second == TriggerState::Triggered;
    };

    for (ActiveMappingContext& context : stack_.Active()) {
        for (ResolvedMapping& mapping : context.mappings) {
            const auto keyIndex = static_cast<std::uint16_t>(mapping.key);
            if (consumedKeys[keyIndex]) {
                continue; // A higher-priority context already claimed this key.
            }

            const MappingEvaluationInput evalInput{
                .valueType = mapping.valueType,
                .rawValue = deviceState_.GetValue(mapping.key),
                .modifiers = mapping.modifiers,
                .triggers = mapping.triggers,
                .chordActionNames = mapping.chordActionNames,
            };
            const MappingEvaluationResult result = evaluator.Evaluate(
                evalInput, deltaSeconds, mapping.modifierState, mapping.triggerStates, chordSatisfied);

            InputActionState& actionState = actionStates_[mapping.actionName];
            actionState.value.type = mapping.valueType;
            if (result.state != TriggerState::None) {
                actionState.value.x += result.value.x;
                actionState.value.y += result.value.y;
                actionState.value.z += result.value.z;
            }
            actionState.combined = MaxState(actionState.combined, result.state);

            if (result.state == TriggerState::Triggered && mapping.consumeInput) {
                consumedKeys[keyIndex] = true;
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

} // namespace kb::input
