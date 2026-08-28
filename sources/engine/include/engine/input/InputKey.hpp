#pragma once

#include <cstdint>
#include <string_view>

namespace kb::input {

// Stable, platform-agnostic identifier for a physical key / button / analog axis.
// The numeric values are part of the serialized asset format, so existing entries
// must never be renumbered. New entries append within their reserved range.
//
// A compact enum rather than a string name, so lookups stay allocation-free.
enum class InputKey : std::uint16_t {
    None = 0,

    // --- Keyboard letters (1..26) ---
    A = 1, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

    // --- Keyboard digits (40..49) ---
    Num0 = 40, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,

    // --- Function keys (60..71) ---
    F1 = 60, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

    // --- Navigation / editing (90..) ---
    Escape = 90,
    Tab,
    CapsLock,
    Space,
    Enter,
    Backspace,
    Delete,
    Insert,
    Home,
    End,
    PageUp,
    PageDown,
    ArrowUp,
    ArrowDown,
    ArrowLeft,
    ArrowRight,

    // --- Modifiers (130..) ---
    LeftShift = 130,
    RightShift,
    LeftControl,
    RightControl,
    LeftAlt,
    RightAlt,

    // --- Mouse buttons (200..) ---
    MouseLeft = 200,
    MouseRight,
    MouseMiddle,
    MouseThumb1,
    MouseThumb2,

    // --- Mouse analog axes (220..) ---
    MouseX = 220,    // Horizontal pointer delta.
    MouseY,          // Vertical pointer delta.
    MouseWheel,      // Wheel delta.

    // --- Gamepad buttons (300..) ---
    GamepadFaceBottom = 300, // A / Cross
    GamepadFaceRight,        // B / Circle
    GamepadFaceLeft,         // X / Square
    GamepadFaceTop,          // Y / Triangle
    GamepadDPadUp,
    GamepadDPadDown,
    GamepadDPadLeft,
    GamepadDPadRight,
    GamepadLeftShoulder,
    GamepadRightShoulder,
    GamepadLeftThumb,        // Left stick click
    GamepadRightThumb,       // Right stick click
    GamepadStart,
    GamepadBack,

    // --- Gamepad analog axes (340..) ---
    GamepadLeftStickX = 340,
    GamepadLeftStickY,
    GamepadRightStickX,
    GamepadRightStickY,
    GamepadLeftTrigger,
    GamepadRightTrigger,

    // --- Touch (400..) ---
    // Normalized digital signal: true while at least one touch point is active.
    // Mirrors how a single mouse button stands in for "the pointer is down";
    // per-point position/id/phase live in InputDeviceState::TouchPoints, not as
    // individual InputKeys (there is no fixed number of "the 3rd finger" keys).
    TouchDown = 400,

    // Upper bound for storage sizing; must stay above the largest value above.
    Count = 512,
};

// Which physical device family a key belongs to - the normalized taxonomy
// LIB-116 introduces so device kinds are a real, named concept instead of an
// implicit numeric-range convention.
enum class InputDeviceKind : std::uint8_t {
    Keyboard,
    Mouse,
    Gamepad,
    Touch,
};

[[nodiscard]] constexpr InputDeviceKind DeviceKindOf(InputKey key) noexcept {
    const auto raw = static_cast<std::uint16_t>(key);
    if (raw >= static_cast<std::uint16_t>(InputKey::GamepadFaceBottom) && raw < static_cast<std::uint16_t>(InputKey::TouchDown)) {
        return InputDeviceKind::Gamepad;
    }
    if (raw == static_cast<std::uint16_t>(InputKey::TouchDown)) {
        return InputDeviceKind::Touch;
    }
    if (raw >= static_cast<std::uint16_t>(InputKey::MouseLeft) && raw < static_cast<std::uint16_t>(InputKey::GamepadFaceBottom)) {
        return InputDeviceKind::Mouse;
    }
    return InputDeviceKind::Keyboard;
}

// Analog keys report a continuous value rather than a pressed/released edge.
[[nodiscard]] constexpr bool IsAnalogKey(InputKey key) noexcept {
    switch (key) {
        case InputKey::MouseX:
        case InputKey::MouseY:
        case InputKey::MouseWheel:
        case InputKey::GamepadLeftStickX:
        case InputKey::GamepadLeftStickY:
        case InputKey::GamepadRightStickX:
        case InputKey::GamepadRightStickY:
        case InputKey::GamepadLeftTrigger:
        case InputKey::GamepadRightTrigger:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] std::string_view ToString(InputKey key) noexcept;

// Parses the canonical string form produced by ToString. Returns InputKey::None on miss.
[[nodiscard]] InputKey ParseInputKey(std::string_view text) noexcept;

} // namespace kb::input
