#pragma once

#if defined(_WIN32)

#include <cstdint>
#include <span>

namespace kb::input {

enum class InputKey : std::uint16_t;

struct Win32KeyBinding {
    InputKey key;
    int virtualKey;
};

struct Win32GamepadButtonBinding {
    InputKey key;
    std::uint16_t mask;
};

// Single source of truth for Win32 virtual-key and XInput-button mappings.
class Win32InputKeyMap {
public:
    [[nodiscard]] static std::span<const Win32KeyBinding> KeyboardAndMouse() noexcept;
    [[nodiscard]] static std::span<const Win32GamepadButtonBinding> GamepadButtons() noexcept;
    [[nodiscard]] static InputKey InputKeyForVirtualKey(int virtualKey) noexcept;
};

} // namespace kb::input

#endif
