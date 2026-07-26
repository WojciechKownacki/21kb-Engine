#include "engine/platform/win32/Win32InputCollector.hpp"

#if defined(_WIN32)

#include "engine/input/InputDeviceState.hpp"
#include "engine/input/InputKey.hpp"
#include "engine/platform/win32/Win32InputKeyMap.hpp"

#include <Xinput.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace kb::input {
namespace {

[[nodiscard]] bool KeyDown(int virtualKey) noexcept {
    return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

[[nodiscard]] bool OwnerProcessIsForeground(HWND ownerWindow) noexcept {
    const HWND foreground = GetForegroundWindow();
    if (foreground == nullptr || ownerWindow == nullptr) {
        return false;
    }
    // Accept any window owned by this process, including editor floating panels.
    DWORD foregroundProcess = 0U;
    static_cast<void>(GetWindowThreadProcessId(foreground, &foregroundProcess));
    return foregroundProcess == GetCurrentProcessId();
}

struct StickAxes {
    float x = 0.0F;
    float y = 0.0F;
};

[[nodiscard]] StickAxes NormalizeStick(SHORT rawX, SHORT rawY, SHORT deadZone) noexcept {
    const float x = static_cast<float>(rawX);
    const float y = static_cast<float>(rawY);
    const float magnitude = std::sqrt((x * x) + (y * y));
    if (magnitude <= static_cast<float>(deadZone)) {
        return {};
    }
    constexpr float kMaxMagnitude = 32767.0F;
    const float clamped = std::min(magnitude, kMaxMagnitude);
    const float scaled = (clamped - static_cast<float>(deadZone)) /
        (kMaxMagnitude - static_cast<float>(deadZone));
    return StickAxes{.x = (x / magnitude) * scaled, .y = (y / magnitude) * scaled};
}

[[nodiscard]] float NormalizeTrigger(BYTE value) noexcept {
    if (value <= XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
        return 0.0F;
    }
    return static_cast<float>(value - XINPUT_GAMEPAD_TRIGGER_THRESHOLD) /
        (255.0F - static_cast<float>(XINPUT_GAMEPAD_TRIGGER_THRESHOLD));
}

void CollectGamepads(InputDeviceState& state) noexcept {
    static_assert(InputDeviceState::kMaxGamepads <= XUSER_MAX_COUNT,
        "Polling more gamepad slots than XInput supports");
    for (std::uint8_t index = 0U; index < InputDeviceState::kMaxGamepads; ++index) {
        XINPUT_STATE gamepadState{};
        const bool connected = XInputGetState(index, &gamepadState) == ERROR_SUCCESS;
        state.SetGamepadConnected(index, connected);
        if (!connected) {
            continue;
        }

        const XINPUT_GAMEPAD& pad = gamepadState.Gamepad;
        for (const Win32GamepadButtonBinding& binding : Win32InputKeyMap::GamepadButtons()) {
            state.SetKeyDown(binding.key, (pad.wButtons & binding.mask) != 0, index);
        }
        const StickAxes left = NormalizeStick(
            pad.sThumbLX, pad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        const StickAxes right = NormalizeStick(
            pad.sThumbRX, pad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
        state.SetAnalog(InputKey::GamepadLeftStickX, left.x, index);
        state.SetAnalog(InputKey::GamepadLeftStickY, left.y, index);
        state.SetAnalog(InputKey::GamepadRightStickX, right.x, index);
        state.SetAnalog(InputKey::GamepadRightStickY, right.y, index);
        state.SetAnalog(InputKey::GamepadLeftTrigger, NormalizeTrigger(pad.bLeftTrigger), index);
        state.SetAnalog(InputKey::GamepadRightTrigger, NormalizeTrigger(pad.bRightTrigger), index);
    }
}

} // namespace

void Win32InputCollector::Collect(InputDeviceState& state, HWND ownerWindow) noexcept {
    state.Reset();
    const bool focused = OwnerProcessIsForeground(ownerWindow);
    state.SetHasFocus(focused);
    if (!focused) {
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

        POINT client = cursor;
        if (ScreenToClient(ownerWindow, &client) != 0) {
            state.SetPointerPosition(static_cast<float>(client.x), static_cast<float>(client.y));
        }
    }

    CollectGamepads(state);
}

} // namespace kb::input

#endif
