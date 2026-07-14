#pragma once

#include <cstdint>

namespace kb::input {

// Phase of a single touch contact within its lifetime, mirroring the shape of
// Win32's WM_TOUCH / TOUCHEVENTF flags without depending on Win32 types here.
enum class InputTouchPhase : std::uint8_t {
    Began,
    Moved,
    Ended,
};

// One active touch contact. `id` is stable across frames for the same finger
// (as long as it stays down) so callers can track a specific contact through
// Began -> Moved* -> Ended, e.g. to drive a drag gesture.
struct InputTouchPoint {
    std::uint32_t id = 0U;
    float x = 0.0F;
    float y = 0.0F;
    InputTouchPhase phase = InputTouchPhase::Began;
};

} // namespace kb::input
