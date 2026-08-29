#pragma once

#include "engine/input/InputActionValueType.hpp"

#include <cmath>

namespace kb::input {

// The resolved value of an input action for the current frame.
// Stores up to three axes plus the declared value type, in one small
// value struct. Self-contained (no scene math dependency).
struct InputValue {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    InputActionValueType type = InputActionValueType::Bool;

    [[nodiscard]] constexpr bool AsBool() const noexcept {
        return x != 0.0F;
    }

    [[nodiscard]] constexpr float AsAxis1D() const noexcept {
        return x;
    }

    // Returns the magnitude across the active axes; useful for "is this action
    // doing anything" checks regardless of value type.
    [[nodiscard]] float Magnitude() const noexcept {
        switch (type) {
            case InputActionValueType::Bool:
            case InputActionValueType::Axis1D:
                return std::fabs(x);
            case InputActionValueType::Axis2D:
                return std::sqrt((x * x) + (y * y));
            case InputActionValueType::Axis3D:
                return std::sqrt((x * x) + (y * y) + (z * z));
        }
        return std::fabs(x);
    }

    [[nodiscard]] constexpr bool IsNonZero() const noexcept {
        return x != 0.0F || y != 0.0F || z != 0.0F;
    }

    // Clamps the stored axes to the channels implied by the value type,
    // zeroing axes that the action does not carry.
    void ClampToType() noexcept {
        const std::uint8_t axes = AxisCount(type);
        if (axes < 3U) {
            z = 0.0F;
        }
        if (axes < 2U) {
            y = 0.0F;
        }
    }
};

} // namespace kb::input
