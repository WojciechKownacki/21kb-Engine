#include "engine/input/InputKey.hpp"

#include <array>
#include <utility>

namespace kb::input {
namespace {

struct InputKeyName {
    InputKey key;
    std::string_view name;
};

// Canonical name table. Order is irrelevant; lookups are linear which is fine
// for the editor / asset tooling that uses these (never on a hot path).
constexpr std::array<InputKeyName, 78> kInputKeyNames{{
    {InputKey::A, "A"}, {InputKey::B, "B"}, {InputKey::C, "C"}, {InputKey::D, "D"},
    {InputKey::E, "E"}, {InputKey::F, "F"}, {InputKey::G, "G"}, {InputKey::H, "H"},
    {InputKey::I, "I"}, {InputKey::J, "J"}, {InputKey::K, "K"}, {InputKey::L, "L"},
    {InputKey::M, "M"}, {InputKey::N, "N"}, {InputKey::O, "O"}, {InputKey::P, "P"},
    {InputKey::Q, "Q"}, {InputKey::R, "R"}, {InputKey::S, "S"}, {InputKey::T, "T"},
    {InputKey::U, "U"}, {InputKey::V, "V"}, {InputKey::W, "W"}, {InputKey::X, "X"},
    {InputKey::Y, "Y"}, {InputKey::Z, "Z"},
    {InputKey::Num0, "Num0"}, {InputKey::Num1, "Num1"}, {InputKey::Num2, "Num2"},
    {InputKey::Num3, "Num3"}, {InputKey::Num4, "Num4"}, {InputKey::Num5, "Num5"},
    {InputKey::Num6, "Num6"}, {InputKey::Num7, "Num7"}, {InputKey::Num8, "Num8"},
    {InputKey::Num9, "Num9"},
    {InputKey::F1, "F1"}, {InputKey::F2, "F2"}, {InputKey::F3, "F3"}, {InputKey::F4, "F4"},
    {InputKey::F5, "F5"}, {InputKey::F6, "F6"}, {InputKey::F7, "F7"}, {InputKey::F8, "F8"},
    {InputKey::F9, "F9"}, {InputKey::F10, "F10"}, {InputKey::F11, "F11"}, {InputKey::F12, "F12"},
    {InputKey::Escape, "Escape"}, {InputKey::Tab, "Tab"}, {InputKey::CapsLock, "CapsLock"},
    {InputKey::Space, "Space"}, {InputKey::Enter, "Enter"}, {InputKey::Backspace, "Backspace"},
    {InputKey::Delete, "Delete"}, {InputKey::Insert, "Insert"}, {InputKey::Home, "Home"},
    {InputKey::End, "End"}, {InputKey::PageUp, "PageUp"}, {InputKey::PageDown, "PageDown"},
    {InputKey::ArrowUp, "ArrowUp"}, {InputKey::ArrowDown, "ArrowDown"},
    {InputKey::ArrowLeft, "ArrowLeft"}, {InputKey::ArrowRight, "ArrowRight"},
    {InputKey::LeftShift, "LeftShift"}, {InputKey::RightShift, "RightShift"},
    {InputKey::LeftControl, "LeftControl"}, {InputKey::RightControl, "RightControl"},
    {InputKey::LeftAlt, "LeftAlt"}, {InputKey::RightAlt, "RightAlt"},
    {InputKey::MouseLeft, "MouseLeft"}, {InputKey::MouseRight, "MouseRight"},
    {InputKey::MouseMiddle, "MouseMiddle"}, {InputKey::MouseThumb1, "MouseThumb1"},
    {InputKey::MouseThumb2, "MouseThumb2"},
    {InputKey::MouseX, "MouseX"}, {InputKey::MouseY, "MouseY"}, {InputKey::MouseWheel, "MouseWheel"},
}};

// Gamepad names are split into a second table to keep each within the array size
// above readable; combined lookups walk both.
constexpr std::array<InputKeyName, 20> kGamepadKeyNames{{
    {InputKey::GamepadFaceBottom, "GamepadFaceBottom"},
    {InputKey::GamepadFaceRight, "GamepadFaceRight"},
    {InputKey::GamepadFaceLeft, "GamepadFaceLeft"},
    {InputKey::GamepadFaceTop, "GamepadFaceTop"},
    {InputKey::GamepadDPadUp, "GamepadDPadUp"},
    {InputKey::GamepadDPadDown, "GamepadDPadDown"},
    {InputKey::GamepadDPadLeft, "GamepadDPadLeft"},
    {InputKey::GamepadDPadRight, "GamepadDPadRight"},
    {InputKey::GamepadLeftShoulder, "GamepadLeftShoulder"},
    {InputKey::GamepadRightShoulder, "GamepadRightShoulder"},
    {InputKey::GamepadLeftThumb, "GamepadLeftThumb"},
    {InputKey::GamepadRightThumb, "GamepadRightThumb"},
    {InputKey::GamepadStart, "GamepadStart"},
    {InputKey::GamepadBack, "GamepadBack"},
    {InputKey::GamepadLeftStickX, "GamepadLeftStickX"},
    {InputKey::GamepadLeftStickY, "GamepadLeftStickY"},
    {InputKey::GamepadRightStickX, "GamepadRightStickX"},
    {InputKey::GamepadRightStickY, "GamepadRightStickY"},
    {InputKey::GamepadLeftTrigger, "GamepadLeftTrigger"},
    {InputKey::GamepadRightTrigger, "GamepadRightTrigger"},
}};

constexpr std::array<InputKeyName, 1> kTouchKeyNames{{
    {InputKey::TouchDown, "TouchDown"},
}};

} // namespace

std::string_view ToString(InputKey key) noexcept {
    for (const InputKeyName& entry : kInputKeyNames) {
        if (entry.key == key) {
            return entry.name;
        }
    }
    for (const InputKeyName& entry : kGamepadKeyNames) {
        if (entry.key == key) {
            return entry.name;
        }
    }
    for (const InputKeyName& entry : kTouchKeyNames) {
        if (entry.key == key) {
            return entry.name;
        }
    }
    return "None";
}

InputKey ParseInputKey(std::string_view text) noexcept {
    for (const InputKeyName& entry : kInputKeyNames) {
        if (entry.name == text) {
            return entry.key;
        }
    }
    for (const InputKeyName& entry : kGamepadKeyNames) {
        if (entry.name == text) {
            return entry.key;
        }
    }
    for (const InputKeyName& entry : kTouchKeyNames) {
        if (entry.name == text) {
            return entry.key;
        }
    }
    return InputKey::None;
}

} // namespace kb::input
