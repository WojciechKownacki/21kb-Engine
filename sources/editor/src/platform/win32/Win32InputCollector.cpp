#include "platform/win32/Win32InputCollector.hpp"

#if defined(_WIN32)

#include "engine/input/InputDeviceState.hpp"
#include "engine/input/InputKey.hpp"

#include <Xinput.h>

#include <array>
#include <cstdint>

namespace kb::editor {
namespace {

using kb::input::InputKey;

struct KeyBinding {
    InputKey key;
    int virtualKey;
};

// Digital key -> Win32 virtual key. Letters/digits use their ASCII codes.
constexpr std::array<KeyBinding, 70> kKeyBindings{{
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

struct GamepadButtonBinding {
    InputKey key;
    WORD mask;
};

constexpr std::array<GamepadButtonBinding, 14> kGamepadButtons{{
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

[[nodiscard]] bool KeyDown(int virtualKey) noexcept {
    return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

[[nodiscard]] bool EditorIsForeground(HWND editorWindow) noexcept {
    const HWND foreground = GetForegroundWindow();
    if (foreground == nullptr || editorWindow == nullptr) {
        return false;
    }
    // Accept any window owned by this process (main window + floating panels).
    DWORD foregroundProcess = 0U;
    static_cast<void>(GetWindowThreadProcessId(foreground, &foregroundProcess));
    return foregroundProcess == GetCurrentProcessId();
}

[[nodiscard]] float NormalizeStick(SHORT value) noexcept {
    return static_cast<float>(value) / 32767.0F;
}

[[nodiscard]] float NormalizeTrigger(BYTE value) noexcept {
    return static_cast<float>(value) / 255.0F;
}

void CollectGamepad(kb::input::InputDeviceState& state) noexcept {
    XINPUT_STATE gamepadState{};
    if (XInputGetState(0, &gamepadState) != ERROR_SUCCESS) {
        return;
    }
    const XINPUT_GAMEPAD& pad = gamepadState.Gamepad;
    for (const GamepadButtonBinding& binding : kGamepadButtons) {
        state.SetKeyDown(binding.key, (pad.wButtons & binding.mask) != 0);
    }
    state.SetAnalog(InputKey::GamepadLeftStickX, NormalizeStick(pad.sThumbLX));
    state.SetAnalog(InputKey::GamepadLeftStickY, NormalizeStick(pad.sThumbLY));
    state.SetAnalog(InputKey::GamepadRightStickX, NormalizeStick(pad.sThumbRX));
    state.SetAnalog(InputKey::GamepadRightStickY, NormalizeStick(pad.sThumbRY));
    state.SetAnalog(InputKey::GamepadLeftTrigger, NormalizeTrigger(pad.bLeftTrigger));
    state.SetAnalog(InputKey::GamepadRightTrigger, NormalizeTrigger(pad.bRightTrigger));
}

} // namespace

kb::input::InputKey Win32InputKeyFromVirtualKey(int virtualKey) noexcept {
    for (const KeyBinding& binding : kKeyBindings) {
        if (binding.virtualKey == virtualKey) {
            return binding.key;
        }
    }
    return InputKey::None;
}

void Win32InputCollector::Collect(kb::input::InputDeviceState& state, HWND editorWindow) noexcept {
    state.Reset();
    if (!EditorIsForeground(editorWindow)) {
        hasPreviousMouse_ = false;
        return;
    }

    for (const KeyBinding& binding : kKeyBindings) {
        state.SetKeyDown(binding.key, KeyDown(binding.virtualKey));
    }

    POINT cursor{};
    if (GetCursorPos(&cursor) != 0) {
        if (hasPreviousMouse_) {
            state.SetAnalog(InputKey::MouseX, static_cast<float>(cursor.x - previousMouse_.x));
            state.SetAnalog(InputKey::MouseY, static_cast<float>(cursor.y - previousMouse_.y));
        }
        previousMouse_ = cursor;
        hasPreviousMouse_ = true;
    }

    CollectGamepad(state);
}

} // namespace kb::editor

#endif
