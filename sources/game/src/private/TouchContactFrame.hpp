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
            if (points_[index].id == id && input::IsTouchActive(points_[index].phase)) return true;
        }
        return false;
    }

    void Update(std::uint32_t id, float x, float y, input::InputTouchPhase phase) noexcept {
        for (std::size_t index = count_; index > 0U; --index) {
            auto& point = points_[index - 1U];
            if (point.id != id || input::IsTouchTerminal(point.phase)) continue;
            if (pendingEnd_[index - 1U]) {
                if (phase == input::InputTouchPhase::Cancelled) {
                    point.phase = phase;
                    pendingEnd_[index - 1U] = false;
                    return;
                }
                continue;
            }
            // A complete tap between frames must expose its press before its release.
            if (point.phase == input::InputTouchPhase::Began && phase == input::InputTouchPhase::Ended) {
                ends_[index - 1U] = input::InputTouchPoint{.id = id, .x = x, .y = y, .phase = phase};
                pendingEnd_[index - 1U] = true;
                return;
            }
            point.x = x;
            point.y = y;
            if (phase != input::InputTouchPhase::Moved || point.phase != input::InputTouchPhase::Began) point.phase = phase;
            return;
        }
        // Unpaired moves cannot revive cancelled contacts. Pending terminal
        // samples remain in the frame while other fingers continue moving.
        if (phase == input::InputTouchPhase::Began && count_ < points_.size()) {
            pendingEnd_[count_] = false;
            points_[count_++] = input::InputTouchPoint{.id = id, .x = x, .y = y, .phase = phase};
        }
    }

    void SetViewport(std::uint32_t width, std::uint32_t height) noexcept {
        if (width != width_ || height != height_) Cancel();
        width_ = width;
        height_ = height;
    }

    void Cancel(bool preserveCompleted = false) noexcept {
        for (std::size_t index = 0U; index < count_; ++index) {
            if (preserveCompleted && pendingEnd_[index]) continue;
            if (input::IsTouchActive(points_[index].phase)) points_[index].phase = input::InputTouchPhase::Cancelled;
            pendingEnd_[index] = false;
        }
    }

    void Publish(input::InputDeviceState& state) const noexcept {
        state.SetPointerViewportExtent(width_, height_);
        std::array<input::InputTouchPoint, input::InputDeviceState::kMaxTouchPoints> visible{};
        std::size_t visibleCount = 0U;
        for (std::size_t index = 0U; index < count_; ++index) {
            if (!IsFirstContact(index)) continue;
            visible[visibleCount++] = points_[index];
        }
        state.SetTouchPoints(std::span{visible}.first(visibleCount));
        if (count_ == 0U) return;
        const input::InputTouchPoint* pointer = &visible[0];
        for (std::size_t index = 0U; index < visibleCount; ++index) {
            if (input::IsTouchActive(visible[index].phase)) {
                pointer = &visible[index];
                break;
            }
        }
        state.SetPointerPosition(pointer->x, pointer->y);
    }

    void FinishFrame() noexcept {
        std::array<bool, input::InputDeviceState::kMaxTouchPoints> consumed{};
        for (std::size_t index = 0U; index < count_; ++index) consumed[index] = IsFirstContact(index);
        std::size_t remaining = 0U;
        for (std::size_t index = 0U; index < count_; ++index) {
            auto point = points_[index];
            bool pending = pendingEnd_[index];
            if (consumed[index]) {
                if (input::IsTouchTerminal(point.phase)) continue;
                if (pending) {
                    point = ends_[index];
                    pending = false;
                } else point.phase = input::InputTouchPhase::Moved;
            }
            pendingEnd_[remaining] = pending;
            ends_[remaining] = ends_[index];
            points_[remaining++] = point;
        }
        count_ = remaining;
    }

private:
    [[nodiscard]] bool IsFirstContact(std::size_t index) const noexcept {
        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            if (points_[earlier].id == points_[index].id) return false;
        }
        return true;
    }

    std::array<input::InputTouchPoint, input::InputDeviceState::kMaxTouchPoints> points_{};
    std::array<input::InputTouchPoint, input::InputDeviceState::kMaxTouchPoints> ends_{};
    std::array<bool, input::InputDeviceState::kMaxTouchPoints> pendingEnd_{};
    std::size_t count_ = 0U;
    std::uint32_t width_ = 0U;
    std::uint32_t height_ = 0U;
};

} // namespace kb::game
