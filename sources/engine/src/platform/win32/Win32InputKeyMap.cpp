#include "engine/platform/win32/Win32InputKeyMap.hpp"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Xinput.h>

#include "engine/input/InputKey.hpp"

#include <array>

namespace kb::input {
namespace {

constexpr std::array<Win32KeyBinding, 70> kKeyboardAndMouse{{
    {InputKey::A, 'A'}, {InputKey::B, 'B'}, {InputKey::C, 'C'}, {InputKey::D, 'D'},
    {InputKey::E, 'E'}, {InputKey::F, 'F'}, {InputKey::G, 'G'}, {InputKey::H, 'H'},
    {InputKey::I, 'I'}, {InputKey::J, 'J'}, {InputKey::K, 'K'}, {InputKey::L, 'L'},
    {InputKey::M, 'M'}, {InputKey::N, 'N'}, {InputKey::O, 'O'}, {InputKey::P, 'P'},
    {InputKey::Q, 'Q'}, {InputKey::R, 'R'}, {InputKey::S, 'S'}, {InputKey::T, 'T'},
    {InputKey::U, 'U'}, {InputKey::V, 'V'}, {InputKey::W, 'W'}, {InputKey::X, 'X'},
    {InputKey::Y, 'Y'}, {InputKey::Z, 'Z'},
    {InputKey::Num0, '0'}, {InputKey::Num1, '1'}, {InputKey::Num2, '2'}, {InputKey::Num3, '3'},
    {InputKey::Num4, '4'}, {InputKey::Num5, '5'}, {InputKey::Num6, '6'}, {InputKey::Num7, '7'},
    {InputKey::Num8, '8'}, {InputKey::Num9, '9'},
    {InputKey::F1, VK_F1}, {InputKey::F2, VK_F2}, {InputKey::F3, VK_F3}, {InputKey::F4, VK_F4},
    {InputKey::F5, VK_F5}, {InputKey::F6, VK_F6}, {InputKey::F7, VK_F7}, {InputKey::F8, VK_F8},
    {InputKey::F9, VK_F9}, {InputKey::F10, VK_F10}, {InputKey::F11, VK_F11}, {InputKey::F12, VK_F12},
    {InputKey::Escape, VK_ESCAPE}, {InputKey::Tab, VK_TAB}, {InputKey::Space, VK_SPACE},
    {InputKey::Enter, VK_RETURN}, {InputKey::Backspace, VK_BACK}, {InputKey::Delete, VK_DELETE},
    {InputKey::ArrowUp, VK_UP}, {InputKey::ArrowDown, VK_DOWN},
    {InputKey::ArrowLeft, VK_LEFT}, {InputKey::ArrowRight, VK_RIGHT},
    {InputKey::LeftShift, VK_LSHIFT}, {InputKey::RightShift, VK_RSHIFT},
    {InputKey::LeftControl, VK_LCONTROL}, {InputKey::RightControl, VK_RCONTROL},
    {InputKey::LeftAlt, VK_LMENU}, {InputKey::RightAlt, VK_RMENU},
    {InputKey::MouseLeft, VK_LBUTTON}, {InputKey::MouseRight, VK_RBUTTON},
    {InputKey::MouseMiddle, VK_MBUTTON}, {InputKey::MouseThumb1, VK_XBUTTON1},
    {InputKey::MouseThumb2, VK_XBUTTON2},
}};

constexpr std::array<Win32GamepadButtonBinding, 14> kGamepadButtons{{
    {InputKey::GamepadFaceBottom, XINPUT_GAMEPAD_A},
    {InputKey::GamepadFaceRight, XINPUT_GAMEPAD_B},
    {InputKey::GamepadFaceLeft, XINPUT_GAMEPAD_X},
    {InputKey::GamepadFaceTop, XINPUT_GAMEPAD_Y},
    {InputKey::GamepadDPadUp, XINPUT_GAMEPAD_DPAD_UP},
    {InputKey::GamepadDPadDown, XINPUT_GAMEPAD_DPAD_DOWN},
    {InputKey::GamepadDPadLeft, XINPUT_GAMEPAD_DPAD_LEFT},
    {InputKey::GamepadDPadRight, XINPUT_GAMEPAD_DPAD_RIGHT},
    {InputKey::GamepadLeftShoulder, XINPUT_GAMEPAD_LEFT_SHOULDER},
    {InputKey::GamepadRightShoulder, XINPUT_GAMEPAD_RIGHT_SHOULDER},
    {InputKey::GamepadLeftThumb, XINPUT_GAMEPAD_LEFT_THUMB},
    {InputKey::GamepadRightThumb, XINPUT_GAMEPAD_RIGHT_THUMB},
    {InputKey::GamepadStart, XINPUT_GAMEPAD_START},
    {InputKey::GamepadBack, XINPUT_GAMEPAD_BACK},
}};

} // namespace

std::span<const Win32KeyBinding> Win32InputKeyMap::KeyboardAndMouse() noexcept {
    return kKeyboardAndMouse;
}

std::span<const Win32GamepadButtonBinding> Win32InputKeyMap::GamepadButtons() noexcept {
    return kGamepadButtons;
}

InputKey Win32InputKeyMap::InputKeyForVirtualKey(int virtualKey) noexcept {
    for (const Win32KeyBinding& binding : kKeyboardAndMouse) {
        if (binding.virtualKey == virtualKey) {
            return binding.key;
        }
    }
    return InputKey::None;
}

} // namespace kb::input

#endif
