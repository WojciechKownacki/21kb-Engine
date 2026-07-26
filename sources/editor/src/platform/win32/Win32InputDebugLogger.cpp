#include "platform/win32/Win32InputDebugLogger.hpp"

#if defined(_WIN32)

#include "engine/input/InputKey.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/platform/win32/Win32InputKeyMap.hpp"

#include <array>
#include <cstdio>
#include <string>

namespace kb::editor {
namespace {

[[nodiscard]] std::size_t Index(kb::input::InputKey key) noexcept {
    const auto raw = static_cast<std::size_t>(key);
    return raw < kb::input::InputDeviceState::kKeyCount ? raw : 0U;
}

[[nodiscard]] std::string_view PhaseName(kb::input::InputActionPhase phase) noexcept {
    switch (phase) {
    case kb::input::InputActionPhase::Started:
        return "Started";
    case kb::input::InputActionPhase::Triggered:
        return "Triggered";
    case kb::input::InputActionPhase::Completed:
        return "Completed";
    case kb::input::InputActionPhase::Canceled:
        return "Canceled";
    }
    return "Triggered";
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

    for (const kb::input::Win32KeyBinding& binding : kb::input::Win32InputKeyMap::KeyboardAndMouse()) {
        check(binding.key);
    }
    for (const kb::input::Win32GamepadButtonBinding& binding : kb::input::Win32InputKeyMap::GamepadButtons()) {
        check(binding.key);
    }
}

void Win32InputDebugLogger::LogActionEvents(const std::vector<kb::input::InputActionEvent>& events, const std::function<void(std::string_view)>& report) {
    if (!report) {
        return;
    }
    for (const kb::input::InputActionEvent& event : events) {
        std::array<char, 96> buffer{};
        std::snprintf(buffer.data(), buffer.size(), "%s: %s (%.2f, %.2f, %.2f)",
                      event.action.c_str(), PhaseName(event.phase).data(),
                      static_cast<double>(event.value.x), static_cast<double>(event.value.y), static_cast<double>(event.value.z));
        report(buffer.data());
    }
}

} // namespace kb::editor

#endif
