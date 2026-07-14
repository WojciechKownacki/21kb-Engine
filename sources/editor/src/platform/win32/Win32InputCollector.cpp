#include "platform/win32/Win32InputCollector.hpp"

#if defined(_WIN32)

#include "engine/input/InputDeviceState.hpp"
#include "engine/input/InputKey.hpp"
#include "platform/win32/Win32InputKeyMap.hpp"

#include <Xinput.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

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

struct StickAxes {
    float x = 0.0F;
    float y = 0.0F;
};

// Radial dead zone (XInput recommendation): ignore small magnitudes so a resting
// stick reads exactly zero, then rescale the remainder to a clean 0..1 range so
// there is no value jump at the dead-zone edge.
[[nodiscard]] StickAxes NormalizeStick(SHORT rawX, SHORT rawY, SHORT deadZone) noexcept {
    const float x = static_cast<float>(rawX);
    const float y = static_cast<float>(rawY);
    const float magnitude = std::sqrt((x * x) + (y * y));
    if (magnitude <= static_cast<float>(deadZone)) {
        return StickAxes{};
    }
    constexpr float kMaxMagnitude = 32767.0F;
    const float clamped = std::min(magnitude, kMaxMagnitude);
    const float scaled = (clamped - static_cast<float>(deadZone)) / (kMaxMagnitude - static_cast<float>(deadZone));
    return StickAxes{.x = (x / magnitude) * scaled, .y = (y / magnitude) * scaled};
}

[[nodiscard]] float NormalizeTrigger(BYTE value) noexcept {
    if (value <= XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
        return 0.0F;
    }
    return static_cast<float>(value - XINPUT_GAMEPAD_TRIGGER_THRESHOLD) / (255.0F - static_cast<float>(XINPUT_GAMEPAD_TRIGGER_THRESHOLD));
}

// LIB-116: polls every XInput slot (0..kMaxGamepads-1), not just the first
// connected controller, so local multiplayer can bind different players to
// different physical gamepads (see InputDeviceState's gamepadIndex parameter
// and InputKeyMapping::gamepadIndex). XInputGetState returns
// ERROR_DEVICE_NOT_CONNECTED for an absent slot, which SetKeyDown/SetAnalog's
// untouched default (false/0) already represents correctly.
void CollectGamepad(kb::input::InputDeviceState& state) noexcept {
    static_assert(kb::input::InputDeviceState::kMaxGamepads <= XUSER_MAX_COUNT,
        "Polling more gamepad slots than XInput supports");
    for (std::uint8_t index = 0U; index < kb::input::InputDeviceState::kMaxGamepads; ++index) {
        XINPUT_STATE gamepadState{};
        if (XInputGetState(index, &gamepadState) != ERROR_SUCCESS) {
            continue;
        }
        const XINPUT_GAMEPAD& pad = gamepadState.Gamepad;
        for (const Win32GamepadButtonBinding& binding : Win32InputKeyMap::GamepadButtons()) {
            state.SetKeyDown(binding.key, (pad.wButtons & binding.mask) != 0, index);
        }
        const StickAxes left = NormalizeStick(pad.sThumbLX, pad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        const StickAxes right = NormalizeStick(pad.sThumbRX, pad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
        state.SetAnalog(InputKey::GamepadLeftStickX, left.x, index);
        state.SetAnalog(InputKey::GamepadLeftStickY, left.y, index);
        state.SetAnalog(InputKey::GamepadRightStickX, right.x, index);
        state.SetAnalog(InputKey::GamepadRightStickY, right.y, index);
        state.SetAnalog(InputKey::GamepadLeftTrigger, NormalizeTrigger(pad.bLeftTrigger), index);
        state.SetAnalog(InputKey::GamepadRightTrigger, NormalizeTrigger(pad.bRightTrigger), index);
    }
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
