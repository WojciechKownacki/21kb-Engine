#include "platform/win32/Win32InputCollector.hpp"

#if defined(_WIN32)

#include "engine/input/InputDeviceState.hpp"
#include "engine/input/InputKey.hpp"
#include "platform/win32/Win32InputKeyMap.hpp"

#include <Xinput.h>

namespace kb::editor {
namespace {

using kb::input::InputKey;

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
    for (const Win32GamepadButtonBinding& binding : Win32InputKeyMap::GamepadButtons()) {
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

void Win32InputCollector::Collect(kb::input::InputDeviceState& state, HWND editorWindow) noexcept {
    state.Reset();
    if (!EditorIsForeground(editorWindow)) {
        hasPreviousMouse_ = false;
        return;
    }

    for (const Win32KeyBinding& binding : Win32InputKeyMap::KeyboardAndMouse()) {
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
