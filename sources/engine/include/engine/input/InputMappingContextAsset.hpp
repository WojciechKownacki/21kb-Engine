#pragma once

#include "engine/input/InputKey.hpp"
#include "engine/input/InputModifierDesc.hpp"
#include "engine/input/InputTriggerDesc.hpp"

#include <cstdint>
#include <vector>

namespace kb::input {

// One physical key bound to one action, with its own modifier and trigger stacks.
struct InputKeyMapping {
    // Stable id for this binding slot, independent of which key it currently
    // points to. Runtime rebinding addresses a binding by this id rather than by
    // key, since the key is exactly what rebinding changes; 0 means "unassigned"
    // (bindings authored before this field existed, or added without an id).
    std::uint64_t bindingId = 0U;
    // Stable asset id of the bound InputAction.
    std::uint64_t actionId = 0U;
    InputKey key = InputKey::None;
    // Multiplies the raw key value before modifiers. The primary control for
    // axes: bind W with scale +1 and S with -1 to build a 1D movement axis, or
    // set mouse/stick sensitivity. Buttons normally leave it at 1.
    float scale = 1.0F;
    // Which connected gamepad this binding reads (LIB-116); ignored for
    // keyboard/mouse/touch keys, which are inherently singular. 0 is "the first
    // gamepad" - every mapping authored before this field existed keeps reading
    // exactly the controller it always did.
    std::uint8_t gamepadIndex = 0U;
    std::vector<InputModifierDesc> modifiers;
    std::vector<InputTriggerDesc> triggers;
};

// One key contributing a signed unit to one channel of a composite binding's
// combined value, e.g. D -> +x, A -> -x, W -> +y, S -> -y for a WASD move composite.
struct InputCompositeSlot {
    InputKey key = InputKey::None;
    std::uint8_t axis = 0U; // 0 = x, 1 = y, 2 = z
    float scale = 1.0F;
    // See InputKeyMapping::gamepadIndex; ignored for non-gamepad keys.
    std::uint8_t gamepadIndex = 0U;
};

// Combines several discrete keys into one composite axis value (WASD -> Axis2D,
// a "fly up/down" pair -> Axis1D, etc.) so the group behaves as one 2D/3D
// vector action. Unlike separate InputKeyMappings that are summed independently
// after their own modifier stacks run, a composite's modifier/trigger stack runs
// once against the fully combined vector - so e.g. a single radial DeadZone shapes
// the resultant direction instead of clipping each key's raw scalar alone.
struct InputCompositeBinding {
    // Stable id for this binding slot; see InputKeyMapping::bindingId.
    std::uint64_t bindingId = 0U;
    // Stable asset id of the bound InputAction.
    std::uint64_t actionId = 0U;
    std::vector<InputCompositeSlot> slots;
    std::vector<InputModifierDesc> modifiers;
    std::vector<InputTriggerDesc> triggers;
};

// An InputMappingContext is a prioritizable set of key->action bindings, pushed
// onto the InputSubsystem stack at runtime. It is pure data - no runtime state.
struct InputMappingContextAsset {
    std::vector<InputKeyMapping> mappings;
    std::vector<InputCompositeBinding> composites;
};

} // namespace kb::input
