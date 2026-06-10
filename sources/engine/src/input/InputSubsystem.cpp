#include "engine/input/InputSubsystem.hpp"

#include "engine/input/InputMappingEvaluator.hpp"

#include <algorithm>
#include <span>
#include <utility>

namespace kb::input {

void InputSubsystem::SetResolvers(ActionResolver actionResolver, ContextResolver contextResolver) {
    actionResolver_ = std::move(actionResolver);
    contextResolver_ = std::move(contextResolver);
}

bool InputSubsystem::AddMappingContext(std::uint64_t contextId, std::int32_t priority) {
    if (!actionResolver_ || !contextResolver_) {
        return false;
    }
    const std::shared_ptr<const InputMappingContextAsset> context = contextResolver_(contextId);
    if (context == nullptr) {
        return false;
    }

    ActiveMappingContext active;
    active.contextId = contextId;
    active.priority = priority;
    active.mappings.reserve(context->mappings.size());
    for (const InputKeyMapping& mapping : context->mappings) {
        ResolvedMapping resolved;
        resolved.key = mapping.key;
        resolved.modifiers = mapping.modifiers;
        resolved.triggers = mapping.triggers;
        resolved.triggerStates.resize(mapping.triggers.size());
        resolved.chordActionNames.resize(mapping.triggers.size());

        if (const std::shared_ptr<const InputActionAsset> action = actionResolver_(mapping.actionId)) {
            resolved.actionName = action->name;
            resolved.valueType = action->valueType;
            resolved.consumeInput = action->consumeInput;
        }

        for (std::size_t index = 0U; index < mapping.triggers.size(); ++index) {
            const InputTriggerDesc& trigger = mapping.triggers[index];
            if (trigger.type == InputTriggerType::Chorded && trigger.chordActionId != 0U) {
                if (const std::shared_ptr<const InputActionAsset> chord = actionResolver_(trigger.chordActionId)) {
                    resolved.chordActionNames[index] = chord->name;
                }
            }
        }

        if (!resolved.actionName.empty()) {
            active.mappings.push_back(std::move(resolved));
        }
    }

    RemoveMappingContext(contextId);
    contexts_.push_back(std::move(active));
    SortByPriority();
    return true;
}

void InputSubsystem::RemoveMappingContext(std::uint64_t contextId) {
    std::erase_if(contexts_, [contextId](const ActiveMappingContext& context) {
        return context.contextId == contextId;
    });
}

void InputSubsystem::ClearMappingContexts() noexcept {
    contexts_.clear();
}

bool InputSubsystem::HasMappingContext(std::uint64_t contextId) const noexcept {
    return std::ranges::any_of(contexts_, [contextId](const ActiveMappingContext& context) {
        return context.contextId == contextId;
    });
}

void InputSubsystem::SortByPriority() {
    std::ranges::stable_sort(contexts_, [](const ActiveMappingContext& lhs, const ActiveMappingContext& rhs) {
        return lhs.priority > rhs.priority;
    });
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

    for (ActiveMappingContext& context : contexts_) {
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
