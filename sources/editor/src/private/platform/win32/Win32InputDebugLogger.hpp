#pragma once

#if defined(_WIN32)

#include "engine/input/InputDeviceState.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <string_view>

namespace kb::input {

class InputDeviceState;

} // namespace kb::input

namespace kb::editor {

// Diagnostic: reports every button (keyboard / mouse / gamepad) that transitions
// from up to down between calls, so the user can confirm raw input reaches the
// runtime. Edge-triggered (a held button is reported once). Decoupled from any
// sink via the report callback.
//
// Single responsibility: detect and announce button-press edges.
class Win32InputDebugLogger {
public:
    void LogPresses(const kb::input::InputDeviceState& state, const std::function<void(std::string_view)>& report);

private:
    std::array<bool, kb::input::InputDeviceState::kKeyCount> wasDown_{};
};

} // namespace kb::editor

#endif
