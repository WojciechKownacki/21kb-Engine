#pragma once

#include "engine/input/InputActionValueType.hpp"
#include "engine/input/InputModifierDesc.hpp"
#include "engine/input/InputModifiers.hpp"
#include "engine/input/InputTriggerDesc.hpp"
#include "engine/input/InputTriggers.hpp"
#include "engine/input/InputValue.hpp"

#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace kb::input {

// The data needed to evaluate a single key->action mapping for one frame.
// Decoupled from the subsystem's storage so evaluation can be tested and reused
// in isolation.
struct MappingEvaluationInput {
    InputActionValueType valueType = InputActionValueType::Bool;
    float rawValue = 0.0F;                              // raw device value for the bound key
    std::span<const InputModifierDesc> modifiers;       // modifier stack (applied in order)
    std::span<const InputTriggerDesc> triggers;         // trigger stack (explicit + implicit)
    std::span<const std::string> chordActionNames;      // parallel to triggers; name per Chorded trigger
};

struct MappingEvaluationResult {
    InputValue value{};                  // value after modifiers
    TriggerState state = TriggerState::None; // combined trigger state for the mapping
};

// Evaluates one mapping: applies the modifier stack to the raw value, runs the
// trigger stack (separating explicit drivers from implicit Chorded gates), and
// reports the combined trigger state plus the modified value.
//
// Single responsibility: turn one mapping's raw input into a (value, state) pair.
class InputMappingEvaluator {
public:
    [[nodiscard]] MappingEvaluationResult Evaluate(const MappingEvaluationInput& input, float deltaSeconds,
                                                   ModifierRuntimeState& modifierState,
                                                   std::span<TriggerRuntimeState> triggerStates,
                                                   const std::function<bool(std::string_view)>& chordSatisfied) const;
};

} // namespace kb::input
