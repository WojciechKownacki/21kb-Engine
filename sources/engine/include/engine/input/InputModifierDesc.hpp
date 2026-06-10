#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace kb::input {

// Modifiers transform a mapping's raw value before triggers evaluate it,
// mirroring Unreal's UInputModifier hierarchy. Parameters are stored inline
// (no per-modifier assets) to keep mapping contexts self-contained.
enum class InputModifierType : std::uint8_t {
    Negate,                   // Flips sign per axis (params: bX, bY, bZ as 0/1).
    Scalar,                   // Multiplies per axis (params: scaleX, scaleY, scaleZ).
    DeadZone,                 // Rescales outside a dead zone (params: lower, upper, type).
    SwizzleAxis,              // Reorders axes (params[0]: SwizzleOrder).
    Smooth,                   // Frame-rate smoothing toward the raw value.
    ResponseCurveExponential, // Raises magnitude to an exponent (params: expX, expY, expZ).
    // FovScaling intentionally omitted in v1: it depends on the active camera,
    // which the input subsystem does not yet have access to. Reserve a slot.
    FovScaling,
};

// Axis reorder presets for SwizzleAxis (params[0] cast to this enum).
enum class InputSwizzleOrder : std::uint8_t {
    YXZ, // Swap X and Y (common for mapping vertical mouse to a single axis).
    ZYX,
    XZY,
    YZX,
    ZXY,
};

// Dead zone shape for DeadZone (params[2] cast to this enum).
enum class InputDeadZoneType : std::uint8_t {
    Radial, // Treats the axes as a vector and scales by combined magnitude.
    Axial,  // Scales each axis independently.
};

struct InputModifierDesc {
    InputModifierType type = InputModifierType::Negate;
    std::array<float, 4> params{};
};

[[nodiscard]] constexpr std::string_view ToString(InputModifierType type) noexcept {
    switch (type) {
        case InputModifierType::Negate:
            return "Negate";
        case InputModifierType::Scalar:
            return "Scalar";
        case InputModifierType::DeadZone:
            return "DeadZone";
        case InputModifierType::SwizzleAxis:
            return "SwizzleAxis";
        case InputModifierType::Smooth:
            return "Smooth";
        case InputModifierType::ResponseCurveExponential:
            return "ResponseCurveExponential";
        case InputModifierType::FovScaling:
            return "FovScaling";
    }
    return "Negate";
}

} // namespace kb::input
