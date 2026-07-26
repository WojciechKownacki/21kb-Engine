#pragma once

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace kb::input {

class InputDeviceState;

// Fills an engine InputDeviceState from the Win32 keyboard/mouse and XInput
// devices owned by the foreground process. The collector is shared by every
// Win32 host (editor and standalone player), so platform polling has one
// production implementation.
class Win32InputCollector {
public:
    void Collect(InputDeviceState& state, HWND ownerWindow) noexcept;

private:
    bool hasPreviousMouse_ = false;
    POINT previousMouse_{};
};

} // namespace kb::input

#endif
