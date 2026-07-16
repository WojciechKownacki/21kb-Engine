#include "platform/win32/Win32XInputHapticsBackend.hpp"

#include "engine/input/InputDeviceState.hpp"

#include <windows.h>

#include <Xinput.h>

#include <algorithm>
#include <cmath>

namespace kb::editor {
namespace {

[[nodiscard]] WORD ToMotorMagnitude(float value) noexcept {
    const float clamped = std::isfinite(value) ? std::clamp(value, 0.0F, 1.0F) : 0.0F;
    return static_cast<WORD>(clamped * 65535.0F);
}

[[nodiscard]] bool IsConnected(std::uint32_t gamepadIndex) noexcept {
    XINPUT_STATE state{};
    return XInputGetState(gamepadIndex, &state) == ERROR_SUCCESS;
}

} // namespace

Win32XInputHapticsBackend::~Win32XInputHapticsBackend() {
    StopAll();
}

kb::input::InputHapticsCapability Win32XInputHapticsBackend::Capability(std::uint32_t gamepadIndex) {
    if (gamepadIndex >= kb::input::InputDeviceState::kMaxGamepads) {
        return kb::input::InputHapticsCapability{
            .maxGamepads = kb::input::InputDeviceState::kMaxGamepads,
            .disabledReason = "gamepad index is outside XInput's slot range",
        };
    }
    return kb::input::InputHapticsCapability{
        .supported = true,
        .connected = IsConnected(gamepadIndex),
        .dualMotor = true,
        .maxGamepads = kb::input::InputDeviceState::kMaxGamepads,
        .disabledReason = {},
    };
}

bool Win32XInputHapticsBackend::SetVibration(std::uint32_t gamepadIndex, float lowFrequency, float highFrequency) {
    if (gamepadIndex >= kb::input::InputDeviceState::kMaxGamepads) {
        return false;
    }
    XINPUT_VIBRATION vibration{
        .wLeftMotorSpeed = ToMotorMagnitude(lowFrequency),
        .wRightMotorSpeed = ToMotorMagnitude(highFrequency),
    };
    return XInputSetState(gamepadIndex, &vibration) == ERROR_SUCCESS;
}

void Win32XInputHapticsBackend::StopAll() noexcept {
    for (std::uint32_t index = 0U; index < kb::input::InputDeviceState::kMaxGamepads; ++index) {
        XINPUT_VIBRATION silence{};
        static_cast<void>(XInputSetState(index, &silence));
    }
}

} // namespace kb::editor
