#pragma once

#if defined(_WIN32)

#include <cstdint>
#include <span>

namespace kb::input {

enum class InputKey : std::uint16_t;

} // namespace kb::input

namespace kb::editor {

struct Win32KeyBinding {
    kb::input::InputKey key;
    int virtualKey;
};

struct Win32GamepadButtonBinding {
    kb::input::InputKey key;
    std::uint16_t mask; // XInput button mask
};

// Single source of truth for the Win32 virtual-key / XInput button <-> engine
// InputKey mapping. Both the device collector (polling) and the inspector key
// capture (reverse lookup) read from here.
//
// Single responsibility: the input key mapping table (no device polling, no UI).
class Win32InputKeyMap {
public:
    [[nodiscard]] static std::span<const Win32KeyBinding> KeyboardAndMouse() noexcept;
    [[nodiscard]] static std::span<const Win32GamepadButtonBinding> GamepadButtons() noexcept;
    [[nodiscard]] static kb::input::InputKey InputKeyForVirtualKey(int virtualKey) noexcept;
};

} // namespace kb::editor

#endif
