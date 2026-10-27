#pragma once

#include "engine/input/InputDeviceState.hpp"

#include <array>
#include <cstddef>

namespace kb::game {

// Event-driven hosts can receive both edges before the next scene update.
// Publish each transition for at least one frame, retaining the pointer at
// the event so a later move cannot redirect a queued click.
class DigitalInputFrame {
public:
    static constexpr std::size_t kCapacity = 256U;

    [[nodiscard]] bool Queue(input::InputDeviceState& state, input::InputKey key,
        bool down, bool pointer = false, float x = 0.0F, float y = 0.0F) noexcept {
        const auto index = static_cast<std::size_t>(key);
        if (index >= scheduled_.size() || key == input::InputKey::None ||
            input::DeviceKindOf(key) == input::InputDeviceKind::Gamepad) return false;
        if (scheduled_[index] == down) return true;
        if (count_ == events_.size()) {
            // Cancel an overflowing burst instead of leaving a held button.
            for (std::size_t i = 0; i < scheduled_.size(); ++i) {
                const auto held = static_cast<input::InputKey>(i);
                if (input::DeviceKindOf(held) != input::InputDeviceKind::Gamepad)
                    state.SetKeyDown(held, false);
            }
            Clear();
            return false;
        }
        scheduled_[index] = down;
        events_[(head_ + count_) % events_.size()] = {key, down, pointer, x, y};
        ++count_;
        return true;
    }

    void Publish(input::InputDeviceState& state) noexcept {
        std::array<bool, input::InputDeviceState::kKeyCount> changed{};
        bool pointerChanged = false;
        if (hasPointer_) state.SetPointerPosition(pointerX_, pointerY_);
        while (count_ != 0U) {
            const Event& event = events_[head_];
            const auto index = static_cast<std::size_t>(event.key);
            if (changed[index] || (event.pointer && pointerChanged)) break;
            changed[index] = true;
            state.SetKeyDown(event.key, event.down);
            if (event.pointer) {
                state.SetPointerPosition(event.x, event.y);
                pointerChanged = true;
            }
            head_ = (head_ + 1U) % events_.size();
            --count_;
        }
    }

    void SetPointerPosition(float x, float y) noexcept {
        pointerX_ = x;
        pointerY_ = y;
        hasPointer_ = true;
    }

    void Clear() noexcept {
        head_ = 0U;
        count_ = 0U;
        scheduled_.fill(false);
        hasPointer_ = false;
    }

private:
    struct Event {
        input::InputKey key{};
        bool down = false;
        bool pointer = false;
        float x = 0.0F;
        float y = 0.0F;
    };
    std::array<Event, kCapacity> events_{};
    std::array<bool, input::InputDeviceState::kKeyCount> scheduled_{};
    std::size_t head_ = 0U;
    std::size_t count_ = 0U;
    float pointerX_ = 0.0F;
    float pointerY_ = 0.0F;
    bool hasPointer_ = false;
};

} // namespace kb::game
