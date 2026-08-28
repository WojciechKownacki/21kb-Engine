#pragma once

#include "engine/input/InputModifierDesc.hpp"
#include "engine/input/InputValue.hpp"

#include <span>

namespace kb::input {

// Per-mapping persistent state needed by stateful modifiers (currently Smooth).
struct ModifierRuntimeState {
    float smoothedX = 0.0F;
    float smoothedY = 0.0F;
    float smoothedZ = 0.0F;
    bool hasSmoothed = false;
};

// Applies a single modifier to a value. `dt` and `state` are only consulted by
// stateful modifiers; the caller passes the same `state` across frames.
[[nodiscard]] InputValue ApplyModifier(InputValue value, const InputModifierDesc& modifier, float dt,
                                       ModifierRuntimeState& state);

// Applies an ordered modifier stack (left to right, in declaration order).
[[nodiscard]] InputValue ApplyModifierStack(InputValue value, std::span<const InputModifierDesc> modifiers,
                                            float dt, ModifierRuntimeState& state);

} // namespace kb::input
