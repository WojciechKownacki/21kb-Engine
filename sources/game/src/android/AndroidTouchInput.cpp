#include "AndroidTouchInput.hpp"

#include <game-activity/native_app_glue/android_native_app_glue.h>

#include <cmath>
#include <iterator>
#include <span>

namespace kb::game {

void AndroidTouchInput::Update(std::uint32_t id, float x, float y, input::InputTouchPhase phase) noexcept {
    for (std::size_t index = count_; index > 0U; --index) {
        auto& point = points_[index - 1U];
        if (point.id != id || point.phase == input::InputTouchPhase::Ended) continue;
        point.x = x;
        point.y = y;
        if (phase != input::InputTouchPhase::Moved || point.phase != input::InputTouchPhase::Began) point.phase = phase;
        return;
    }
    // A move after cancellation must not resurrect a contact. Keep terminal
    // samples until the scene consumes a frame, even if another finger moves.
    if (phase == input::InputTouchPhase::Began && count_ < points_.size()) {
        points_[count_++] = input::InputTouchPoint{.id = id, .x = x, .y = y, .phase = phase};
    }
}

bool AndroidTouchInput::Process(const GameActivityMotionEvent& event) noexcept {
    if ((event.source & AINPUT_SOURCE_TOUCHSCREEN) != AINPUT_SOURCE_TOUCHSCREEN) return false;
    const auto action = event.action & AMOTION_EVENT_ACTION_MASK;
    if (action != AMOTION_EVENT_ACTION_DOWN && action != AMOTION_EVENT_ACTION_POINTER_DOWN &&
        action != AMOTION_EVENT_ACTION_MOVE && action != AMOTION_EVENT_ACTION_UP &&
        action != AMOTION_EVENT_ACTION_POINTER_UP && action != AMOTION_EVENT_ACTION_CANCEL) return false;
    if (event.pointerCount > std::size(event.pointers)) return false;
    const auto actionIndex = static_cast<std::uint32_t>(
        (event.action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
    if (action != AMOTION_EVENT_ACTION_CANCEL && (event.pointerCount == 0U || actionIndex >= event.pointerCount)) return false;
    for (std::uint32_t index = 0U; index < event.pointerCount; ++index) {
        const auto& point = event.pointers[index];
        if (point.id < 0 || !std::isfinite(GameActivityPointerAxes_getX(&point)) ||
            !std::isfinite(GameActivityPointerAxes_getY(&point))) return false;
        for (std::uint32_t earlier = 0U; earlier < index; ++earlier) {
            if (event.pointers[earlier].id == point.id) return false;
        }
    }
    if (action == AMOTION_EVENT_ACTION_DOWN) Cancel();
    for (std::uint32_t index = 0U; index < event.pointerCount; ++index) {
        auto phase = input::InputTouchPhase::Moved;
        if (index == actionIndex &&
            (action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_POINTER_DOWN)) {
            phase = input::InputTouchPhase::Began;
        } else if (action == AMOTION_EVENT_ACTION_CANCEL || (index == actionIndex &&
            (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_POINTER_UP))) {
            phase = input::InputTouchPhase::Ended;
        }
        const auto& point = event.pointers[index];
        Update(static_cast<std::uint32_t>(point.id), GameActivityPointerAxes_getX(&point),
            GameActivityPointerAxes_getY(&point), phase);
    }
    if (action == AMOTION_EVENT_ACTION_CANCEL) Cancel();
    return true;
}

void AndroidTouchInput::HandleAppCommand(std::int32_t command) noexcept {
    switch (command) {
    case APP_CMD_LOST_FOCUS:
    case APP_CMD_PAUSE:
    case APP_CMD_STOP:
    case APP_CMD_TERM_WINDOW:
    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_CONFIG_CHANGED:
        Cancel();
        break;
    default:
        break;
    }
}

void AndroidTouchInput::SetViewport(std::uint32_t width, std::uint32_t height) noexcept {
    if (width != width_ || height != height_) Cancel();
    width_ = width;
    height_ = height;
}

void AndroidTouchInput::Cancel() noexcept {
    for (std::size_t index = 0U; index < count_; ++index) points_[index].phase = input::InputTouchPhase::Ended;
}

void AndroidTouchInput::Publish(input::InputDeviceState& state) const noexcept {
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

void AndroidTouchInput::FinishFrame() noexcept {
    std::size_t remaining = 0U;
    for (std::size_t index = 0U; index < count_; ++index) {
        auto point = points_[index];
        if (point.phase == input::InputTouchPhase::Ended) continue;
        point.phase = input::InputTouchPhase::Moved;
        points_[remaining++] = point;
    }
    count_ = remaining;
}

} // namespace kb::game
