#pragma once

#include "engine/input/InputDeviceState.hpp"

#include <emscripten/html5.h>

namespace kb::game {

void ApplyWebGamepadSample(input::InputDeviceState& state, std::uint8_t slot,
    const EmscriptenGamepadEvent* sample) noexcept;
void PollWebGamepads(input::InputDeviceState& state) noexcept;

} // namespace kb::game
