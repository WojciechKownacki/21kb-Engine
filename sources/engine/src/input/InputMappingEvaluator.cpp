#include "engine/input/InputMappingEvaluator.hpp"

#include <cstddef>

namespace kb::input {

MappingEvaluationResult InputMappingEvaluator::Evaluate(const MappingEvaluationInput& input, float deltaSeconds,
                                                        ModifierRuntimeState& modifierState,
                                                        std::span<TriggerRuntimeState> triggerStates,
                                                        const std::function<bool(std::string_view)>& chordSatisfied) const {
    const InputValue raw{.x = input.rawValue, .y = 0.0F, .z = 0.0F, .type = input.valueType};
    const InputValue modified = ApplyModifierStack(raw, input.modifiers, deltaSeconds, modifierState);
    const float magnitude = modified.Magnitude();

    // Explicit triggers drive firing; implicit (Chorded) triggers gate it.
    TriggerState explicitState = TriggerState::None;
    bool hasExplicit = false;
    bool hasImplicit = false;
    bool implicitSatisfied = true;
    for (std::size_t index = 0; index < input.triggers.size(); ++index) {
        const InputTriggerDesc& trigger = input.triggers[index];
        bool chord = false;
        if (trigger.type == InputTriggerType::Chorded && index < input.chordActionNames.size()) {
            chord = chordSatisfied(input.chordActionNames[index]);
        }
        const TriggerState state = EvaluateTrigger(trigger, triggerStates[index], magnitude, deltaSeconds, chord);
        if (IsImplicitTrigger(trigger.type)) {
            hasImplicit = true;
            implicitSatisfied = implicitSatisfied && state == TriggerState::Triggered;
        } else {
            hasExplicit = true;
            explicitState = MaxState(explicitState, state);
        }
    }

    if (!hasExplicit) {
        // No explicit triggers => implicit "Down" behaviour.
        explicitState = magnitude >= 0.5F ? TriggerState::Triggered : TriggerState::None;
    }
    const TriggerState mappingState = (hasImplicit && !implicitSatisfied) ? TriggerState::None : explicitState;
    return MappingEvaluationResult{.value = modified, .state = mappingState};
}

} // namespace kb::input
