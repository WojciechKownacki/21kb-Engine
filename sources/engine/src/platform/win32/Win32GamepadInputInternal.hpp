#pragma once

#include "engine/platform/win32/Win32GamepadInput.hpp"
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <dinput.h>

namespace kb::input::detail {
void PublishDirectInputSnapshot(const Win32GamepadMapping& mapping,
    const std::array<bool, 8>& availableAxes, const DIJOYSTATE2& sample,
    InputDeviceState& state, std::uint8_t slot);
}
