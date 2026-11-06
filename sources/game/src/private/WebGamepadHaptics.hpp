#pragma once
#include "engine/input/InputHaptics.hpp"

namespace kb::game {
class WebGamepadHaptics final : public input::IInputHapticsBackend {
public:
    input::InputHapticsCapability Capability(std::uint32_t slot) override;
    bool SetVibration(std::uint32_t slot, float low, float high) override;
    void StopAll() noexcept override;
    void Update() noexcept;
};
} // namespace kb::game
