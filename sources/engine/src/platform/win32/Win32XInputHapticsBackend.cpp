#include "engine/platform/win32/Win32XInputHapticsBackend.hpp"

#include "engine/input/InputDeviceState.hpp"
#include "engine/platform/win32/Win32GamepadInput.hpp"

#include <windows.h>

#include <Xinput.h>

#include <algorithm>
#include <cmath>

namespace kb::input {
namespace {

[[nodiscard]] WORD ToMotorMagnitude(float value) noexcept {
    const float clamped =
        std::isfinite(value) ? std::clamp(value, 0.0F, 1.0F) : 0.0F;
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

InputHapticsCapability Win32XInputHapticsBackend::Capability(
    std::uint32_t gamepadIndex) {
    if (gamepadIndex >= InputDeviceState::kMaxGamepads) {
        return InputHapticsCapability{
            .maxGamepads = InputDeviceState::kMaxGamepads,
            .disabledReason = "gamepad index is outside XInput's slot range",
        };
    }
    const auto user = Win32GamepadXInputIndex(gamepadIndex);
    const bool supported = user < XUSER_MAX_COUNT;
    return InputHapticsCapability{
        .supported = supported,
        .connected = supported && IsConnected(user),
        .dualMotor = supported,
        .maxGamepads = InputDeviceState::kMaxGamepads,
        .disabledReason = supported ? "" : "controller has no XInput actuator",
    };
}

bool Win32XInputHapticsBackend::SetVibration(
    std::uint32_t gamepadIndex,
    float lowFrequency,
    float highFrequency) {
    if (gamepadIndex >= InputDeviceState::kMaxGamepads) {
        return false;
    }
    XINPUT_VIBRATION vibration{
        .wLeftMotorSpeed = ToMotorMagnitude(lowFrequency),
        .wRightMotorSpeed = ToMotorMagnitude(highFrequency),
    };
    const auto user = Win32GamepadXInputIndex(gamepadIndex);
    return user < XUSER_MAX_COUNT && XInputSetState(user, &vibration) == ERROR_SUCCESS;
}

void Win32XInputHapticsBackend::StopAll() noexcept {
    for (std::uint32_t index = 0U;
         index < InputDeviceState::kMaxGamepads;
         ++index) {
        XINPUT_VIBRATION silence{};
        static_cast<void>(XInputSetState(index, &silence));
    }
}

} // namespace kb::input
