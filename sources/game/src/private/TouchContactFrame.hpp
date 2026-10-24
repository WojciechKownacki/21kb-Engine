#pragma once

#include "engine/input/InputDeviceState.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace kb::game {

class TouchContactFrame {
public:
    [[nodiscard]] bool Contains(std::uint32_t id) const noexcept {
        for (std::size_t index = 0U; index < count_; ++index) {
            if (points_[index].id == id && points_[index].phase != input::InputTouchPhase::Ended) return true;
        }
        return false;
    }

    void Update(std::uint32_t id, float x, float y, input::InputTouchPhase phase) noexcept {
        for (std::size_t index = count_; index > 0U; --index) {
            auto& point = points_[index - 1U];
            if (point.id != id || point.phase == input::InputTouchPhase::Ended) continue;
            point.x = x;
            point.y = y;
            if (phase != input::InputTouchPhase::Moved || point.phase != input::InputTouchPhase::Began) point.phase = phase;
            return;
        }
        // Unpaired moves cannot revive cancelled contacts. Pending terminal
        // samples remain in the frame while other fingers continue moving.
        if (phase == input::InputTouchPhase::Began && count_ < points_.size()) {
            points_[count_++] = input::InputTouchPoint{.id = id, .x = x, .y = y, .phase = phase};
        }
    }

    void SetViewport(std::uint32_t width, std::uint32_t height) noexcept {
        if (width != width_ || height != height_) Cancel();
        width_ = width;
        height_ = height;
    }

    void Cancel() noexcept {
        for (std::size_t index = 0U; index < count_; ++index) points_[index].phase = input::InputTouchPhase::Ended;
    }

    void Publish(input::InputDeviceState& state) const noexcept {
        state.SetPointerViewportExtent(width_, height_);
        state.SetTouchPoints(std::span{points_}.first(count_));
        if (count_ == 0U) return;
        const input::InputTouchPoint* pointer = &points_[0];
        for (std::size_t index = 0U; index < count_; ++index) {
            if (points_[index].phase != input::InputTouchPhase::Ended) {
                pointer = &points_[index];
                break;
            }
        }
        state.SetPointerPosition(pointer->x, pointer->y);
    }

    void FinishFrame() noexcept {
        std::size_t remaining = 0U;
        for (std::size_t index = 0U; index < count_; ++index) {
            auto point = points_[index];
            if (point.phase == input::InputTouchPhase::Ended) continue;
            point.phase = input::InputTouchPhase::Moved;
            points_[remaining++] = point;
        }
        count_ = remaining;
    }

private:
    std::array<input::InputTouchPoint, input::InputDeviceState::kMaxTouchPoints> points_{};
    std::size_t count_ = 0U;
    std::uint32_t width_ = 0U;
    std::uint32_t height_ = 0U;
};

} // namespace kb::game
