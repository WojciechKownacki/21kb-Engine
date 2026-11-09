#pragma once

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <array>
#include <cstdint>

namespace kb::input {
class InputDeviceState;

// DirectInput axes: X, Y, Z, Rx, Ry, Rz, slider 0, slider 1.
// Negative indices disable a control. Buttons follow the engine's fourteen
// gamepad keys, beginning with face bottom/right/left/top. POV is combined with
// the four directional buttons. Product mappings apply to newly opened devices.
struct Win32GamepadMapping {
    std::array<int, 6> axes{0, 1, 3, 4, 2, 5};
    std::array<int, 14> buttons{0, 1, 2, 3, -1, -1, -1, -1, 4, 5, 10, 11, 7, 6};
    int pov = 0;
    float deadZone = 0.15F;
};

void ConfigureWin32GamepadMapping(const GUID& product, const Win32GamepadMapping& mapping);
void CollectWin32Gamepads(InputDeviceState& state, HWND window) noexcept;
// Returns UINT32_MAX for an empty slot or a controller without XInput motors.
std::uint32_t Win32GamepadXInputIndex(std::uint32_t slot) noexcept;
} // namespace kb::input
#endif
