#include "platform/win32/Win32InputDebugLogger.hpp"

#if defined(_WIN32)

#include "engine/input/InputKey.hpp"
#include "platform/win32/Win32InputKeyMap.hpp"

namespace kb::editor {
namespace {

[[nodiscard]] std::size_t Index(kb::input::InputKey key) noexcept {
    const auto raw = static_cast<std::size_t>(key);
    return raw < kb::input::InputDeviceState::kKeyCount ? raw : 0U;
}

} // namespace

void Win32InputDebugLogger::LogPresses(const kb::input::InputDeviceState& state, const std::function<void(std::string_view)>& report) {
    const auto check = [&](kb::input::InputKey key) {
        const bool down = state.IsKeyDown(key);
        bool& was = wasDown_[Index(key)];
        if (down && !was && report) {
            report(kb::input::ToString(key));
        }
        was = down;
    };

    for (const Win32KeyBinding& binding : Win32InputKeyMap::KeyboardAndMouse()) {
        check(binding.key);
    }
    for (const Win32GamepadButtonBinding& binding : Win32InputKeyMap::GamepadButtons()) {
        check(binding.key);
    }
}

} // namespace kb::editor

#endif
