#pragma once

#include "engine/input/InputHaptics.hpp"

namespace kb::input {

// Windows XInput actuator for the same 0..3 controller slots polled by
// Win32InputCollector. Destruction always silences all motors so host shutdown
// cannot leave a controller vibrating.
class Win32XInputHapticsBackend final : public IInputHapticsBackend {
public:
    ~Win32XInputHapticsBackend() override;

    [[nodiscard]] InputHapticsCapability Capability(
        std::uint32_t gamepadIndex) override;
    [[nodiscard]] bool SetVibration(
        std::uint32_t gamepadIndex,
        float lowFrequency,
        float highFrequency) override;
    void StopAll() noexcept override;
};

} // namespace kb::input
