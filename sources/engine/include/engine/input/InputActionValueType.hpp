#pragma once

#include <cstdint>
#include <string_view>

namespace kb::input {

// Number of axes an input action carries, mirroring Unreal's EInputActionValueType.
enum class InputActionValueType : std::uint8_t {
    Bool,   // Digital on/off (1 axis, treated as 0/1).
    Axis1D, // Single analog axis.
    Axis2D, // Two analog axes (e.g. movement, look).
    Axis3D, // Three analog axes.
};

[[nodiscard]] constexpr std::uint8_t AxisCount(InputActionValueType type) noexcept {
    switch (type) {
        case InputActionValueType::Bool:
        case InputActionValueType::Axis1D:
            return 1U;
        case InputActionValueType::Axis2D:
            return 2U;
        case InputActionValueType::Axis3D:
            return 3U;
    }
    return 1U;
}

[[nodiscard]] constexpr std::string_view ToString(InputActionValueType type) noexcept {
    switch (type) {
        case InputActionValueType::Bool:
            return "Bool";
        case InputActionValueType::Axis1D:
            return "Axis1D";
        case InputActionValueType::Axis2D:
            return "Axis2D";
        case InputActionValueType::Axis3D:
            return "Axis3D";
    }
    return "Bool";
}

} // namespace kb::input
