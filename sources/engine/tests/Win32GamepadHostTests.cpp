#include "engine/platform/win32/Win32GamepadInput.hpp"
#include "engine/platform/win32/Win32XInputHapticsBackend.hpp"
#include "engine/input/InputDeviceState.hpp"
#include "../src/platform/win32/Win32GamepadInputInternal.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() {
    const HWND window = CreateWindowExW(0, L"STATIC", L"Controller host test", WS_OVERLAPPED,
        0, 0, 64, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window) return 1;
    int result = 1;
    try {
        kb::input::InputDeviceState state;
        kb::input::Win32GamepadMapping mapping;
        std::array<bool, 8> axes;
        axes.fill(true);
        DIJOYSTATE2 sample{};
        sample.lX = 49151; sample.lY = 8192; sample.lRx = 24576; sample.lRy = 45055;
        sample.lZ = 39321; sample.lRz = 52428;
        sample.rgbButtons[0] = 0x80; sample.rgbButtons[7] = 0x80;
        for (auto& pov : sample.rgdwPOV) pov = 0xffffffff;
        sample.rgdwPOV[0] = 4500;
        kb::input::detail::PublishDirectInputSnapshot(mapping, axes, sample, state, 2);
        const auto closeTo = [&](kb::input::InputKey key, float expected) {
            return std::abs(state.GetValue(key, 2) - expected) < .002F;
        };
        using kb::input::InputKey;
        Require(closeTo(InputKey::GamepadLeftStickX, .5F) && closeTo(InputKey::GamepadLeftStickY, .75F) &&
            closeTo(InputKey::GamepadRightStickX, -.25F) && closeTo(InputKey::GamepadRightStickY, -.375F) &&
            closeTo(InputKey::GamepadLeftTrigger, .6F) && closeTo(InputKey::GamepadRightTrigger, .8F), "DirectInput axes");
        Require(state.IsKeyDown(InputKey::GamepadFaceBottom, 2) && state.IsKeyDown(InputKey::GamepadStart, 2) &&
            state.IsKeyDown(InputKey::GamepadDPadUp, 2) && state.IsKeyDown(InputKey::GamepadDPadRight, 2), "DirectInput buttons and diagonal hat");
        Require(!state.IsKeyDown(InputKey::GamepadFaceBottom, 0), "DirectInput slot isolation");
        state.Reset();
        sample.lX = 34000; sample.rgdwPOV[0] = 0xffffffff;
        mapping.axes[4] = -1; mapping.buttons[0] = 127;
        axes[3] = false;
        kb::input::detail::PublishDirectInputSnapshot(mapping, axes, sample, state, 2);
        Require(closeTo(InputKey::GamepadLeftStickX, 0) && closeTo(InputKey::GamepadRightStickX, 0) &&
            closeTo(InputKey::GamepadLeftTrigger, 0), "DirectInput dead zone and disabled axes");
        Require(!state.IsKeyDown(InputKey::GamepadDPadUp, 2) && !state.IsKeyDown(InputKey::GamepadFaceBottom, 2), "neutral hat and remapped button");
        state.Reset();
        kb::input::Win32XInputHapticsBackend haptics;
        state.SetHasFocus(false);
        for (std::uint8_t slot = 0; slot < 4; ++slot) {
            state.SetKeyDown(kb::input::InputKey::GamepadFaceBottom, true, slot);
            state.SetAnalog(kb::input::InputKey::GamepadLeftStickX, .5F, slot);
        }
        kb::input::CollectWin32Gamepads(state, window);
        std::array<bool, 4> connected{};
        std::array<std::uint32_t, 4> users{};
        for (std::uint8_t slot = 0; slot < 4; ++slot) {
            Require(!state.IsKeyDown(kb::input::InputKey::GamepadFaceBottom, slot) &&
                state.GetValue(kb::input::InputKey::GamepadLeftStickX, slot) == 0, "background input was not released");
            connected[slot] = state.IsGamepadConnected(slot);
            users[slot] = kb::input::Win32GamepadXInputIndex(slot);
            const auto capability = haptics.Capability(slot);
            Require(capability.supported == (users[slot] < 4), "actuator does not match device slot");
            if (!capability.supported) Require(!haptics.SetVibration(slot, 0, 0), "unsupported actuator accepted vibration");
            for (std::uint8_t previous = 0; previous < slot; ++previous)
                Require(users[slot] >= 4 || users[slot] != users[previous], "XInput device assigned twice");
            std::cout << "slot=" << static_cast<unsigned int>(slot) << " connected=" << connected[slot]
                      << " xinput=" << users[slot] << '\n';
        }
        state.SetHasFocus(true);
        kb::input::CollectWin32Gamepads(state, window);
        for (std::uint8_t slot = 0; slot < 4; ++slot) {
            Require(state.IsGamepadConnected(slot) == connected[slot], "focus changed device connection");
            Require(kb::input::Win32GamepadXInputIndex(slot) == users[slot], "focus changed device identity");
        }
        Require(kb::input::Win32GamepadXInputIndex(100) == std::numeric_limits<std::uint32_t>::max(), "invalid slot mapped to a device");
        Require(!haptics.SetVibration(100, 0, 0), "invalid actuator index accepted");
        std::cout << "PASS: native controller discovery, focus release and actuator slot identity\n";
        result = 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; }
    DestroyWindow(window);
    return result;
}
