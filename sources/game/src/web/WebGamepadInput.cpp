#include "WebGamepadInput.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <utility>

namespace kb::game {
namespace {
using input::InputKey;
constexpr std::array kButtons{
    std::pair{InputKey::GamepadFaceBottom, 0},
    std::pair{InputKey::GamepadFaceRight, 1},
    std::pair{InputKey::GamepadFaceLeft, 2},
    std::pair{InputKey::GamepadFaceTop, 3},
    std::pair{InputKey::GamepadLeftShoulder, 4},
    std::pair{InputKey::GamepadRightShoulder, 5},
    std::pair{InputKey::GamepadBack, 8},
    std::pair{InputKey::GamepadStart, 9},
    std::pair{InputKey::GamepadLeftThumb, 10},
    std::pair{InputKey::GamepadRightThumb, 11},
    std::pair{InputKey::GamepadDPadUp, 12},
    std::pair{InputKey::GamepadDPadDown, 13},
    std::pair{InputKey::GamepadDPadLeft, 14},
    std::pair{InputKey::GamepadDPadRight, 15},
};

float Bounded(double value, double minimum) noexcept {
    return std::isfinite(value) ? static_cast<float>(std::clamp(value, minimum, 1.0)) : 0.0F;
}
} // namespace

void ApplyWebGamepadSample(input::InputDeviceState& state, std::uint8_t slot,
    const EmscriptenGamepadEvent* sample) noexcept {
    if (slot >= input::InputDeviceState::kMaxGamepads) return;
    const bool connected = sample != nullptr && sample->connected;
    state.SetGamepadConnected(slot, connected);
    // Only the browser's standard mapping has a portable button/axis contract.
    // Missing, disconnected or unmapped samples must release the previous input.
    const bool mapped = connected && std::memcmp(sample->mapping, "standard", 9U) == 0 &&
        sample->numAxes >= 0 && sample->numAxes <= 64 &&
        sample->numButtons >= 0 && sample->numButtons <= 64;
    for (const auto& [key, button] : kButtons) {
        state.SetKeyDown(key, mapped && button < sample->numButtons && sample->digitalButton[button], slot);
    }
    const auto axis = [&](int index) {
        return mapped && index < sample->numAxes ? Bounded(sample->axis[index], -1.0) : 0.0F;
    };
    const auto trigger = [&](int index) {
        return mapped && index < sample->numButtons ? Bounded(sample->analogButton[index], 0.0) : 0.0F;
    };
    state.SetAnalog(InputKey::GamepadLeftStickX, axis(0), slot);
    state.SetAnalog(InputKey::GamepadLeftStickY, -axis(1), slot);
    state.SetAnalog(InputKey::GamepadRightStickX, axis(2), slot);
    state.SetAnalog(InputKey::GamepadRightStickY, -axis(3), slot);
    state.SetAnalog(InputKey::GamepadLeftTrigger, trigger(6), slot);
    state.SetAnalog(InputKey::GamepadRightTrigger, trigger(7), slot);
}

void PollWebGamepads(input::InputDeviceState& state) noexcept {
    const bool available = state.HasFocus() && emscripten_sample_gamepad_data() == EMSCRIPTEN_RESULT_SUCCESS;
    const int count = available ? emscripten_get_num_gamepads() : 0;
    for (std::uint8_t slot = 0U; slot < input::InputDeviceState::kMaxGamepads; ++slot) {
        EmscriptenGamepadEvent sample{};
        const bool read = slot < count &&
            emscripten_get_gamepad_status(slot, &sample) == EMSCRIPTEN_RESULT_SUCCESS;
        ApplyWebGamepadSample(state, slot, read ? &sample : nullptr);
    }
}

} // namespace kb::game
