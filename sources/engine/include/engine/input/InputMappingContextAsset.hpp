#pragma once

#include "engine/input/InputKey.hpp"
#include "engine/input/InputModifierDesc.hpp"
#include "engine/input/InputTriggerDesc.hpp"

#include <cstdint>
#include <vector>

namespace kb::input {

// One physical key bound to one action, with its own modifier and trigger stacks.
struct InputKeyMapping {
    // Stable asset id of the bound InputAction.
    std::uint64_t actionId = 0U;
    InputKey key = InputKey::None;
    // Multiplies the raw key value before modifiers. The primary control for
    // axes: bind W with scale +1 and S with -1 to build a 1D movement axis, or
    // set mouse/stick sensitivity. Buttons normally leave it at 1.
    float scale = 1.0F;
    std::vector<InputModifierDesc> modifiers;
    std::vector<InputTriggerDesc> triggers;
};

// An InputMappingContext is a prioritizable set of key->action bindings, pushed
// onto the InputSubsystem stack at runtime. Mirrors Unreal's UInputMappingContext.
struct InputMappingContextAsset {
    std::vector<InputKeyMapping> mappings;
};

} // namespace kb::input
