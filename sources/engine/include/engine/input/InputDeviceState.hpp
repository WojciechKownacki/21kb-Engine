#pragma once

#include "engine/input/InputKey.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace kb::input {

// A raw, platform-agnostic snapshot of every input device for a single frame.
//
// The platform layer (e.g. the editor's Win32/XInput collector) fills this each
// frame; the InputSubsystem consumes it during evaluation. It carries no Win32
// types so it can live in the cross-platform engine library.
class InputDeviceState {
public:
    static constexpr std::size_t kKeyCount = static_cast<std::size_t>(InputKey::Count);

    void Reset() noexcept {
        digital_.fill(false);
        analog_.fill(0.0F);
    }

    void SetKeyDown(InputKey key, bool down) noexcept {
        digital_[Index(key)] = down;
    }

    void SetAnalog(InputKey key, float value) noexcept {
        analog_[Index(key)] = value;
    }

    [[nodiscard]] bool IsKeyDown(InputKey key) const noexcept {
        return digital_[Index(key)];
    }

    // Returns the analog value for analog keys, or 1.0/0.0 for digital keys so
    // that a single accessor works for both during action evaluation.
    [[nodiscard]] float GetValue(InputKey key) const noexcept {
        if (IsAnalogKey(key)) {
            return analog_[Index(key)];
        }
        return digital_[Index(key)] ? 1.0F : 0.0F;
    }

private:
    [[nodiscard]] static std::size_t Index(InputKey key) noexcept {
        const auto raw = static_cast<std::size_t>(key);
        return raw < kKeyCount ? raw : 0U;
    }

    std::array<bool, kKeyCount> digital_{};
    std::array<float, kKeyCount> analog_{};
};

} // namespace kb::input
