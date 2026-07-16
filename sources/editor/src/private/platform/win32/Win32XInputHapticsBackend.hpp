#pragma once

#include "engine/input/InputHaptics.hpp"

namespace kb::editor {

// LIB-153: the real Windows haptics actuator - XInputSetState on the same 0..3 gamepad
// slots Win32InputCollector already polls with XInputGetState (the editor owns the xinput
// dependency; the engine library stays platform-free). XInput's platform limits are
// exactly what InputHapticsCapability models: two motor magnitudes per pad, nothing else.
// The destructor (and Play Mode stop) silences every motor - a closed game must never
// leave a pad buzzing.
class Win32XInputHapticsBackend final : public kb::input::IInputHapticsBackend {
public:
    ~Win32XInputHapticsBackend() override;

    [[nodiscard]] kb::input::InputHapticsCapability Capability(std::uint32_t gamepadIndex) override;
    [[nodiscard]] bool SetVibration(std::uint32_t gamepadIndex, float lowFrequency, float highFrequency) override;
    void StopAll() noexcept override;
};

} // namespace kb::editor
