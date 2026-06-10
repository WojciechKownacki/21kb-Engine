#pragma once

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace kb::input {

class InputDeviceState;

} // namespace kb::input

namespace kb::editor {

// Fills an engine InputDeviceState each frame from Win32 keyboard/mouse and
// XInput gamepad state. Lives in the editor (Win32-specific); the engine stays
// platform-agnostic. Carries the previous mouse position to compute deltas.
class Win32InputCollector {
public:
    // Populates `state` from the current device readings. If the editor is not
    // the foreground application, the state is cleared so the game receives no
    // input while another window is focused.
    void Collect(kb::input::InputDeviceState& state, HWND editorWindow) noexcept;

private:
    bool hasPreviousMouse_ = false;
    POINT previousMouse_{};
};

} // namespace kb::editor

#endif
