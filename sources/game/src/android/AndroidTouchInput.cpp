#include "AndroidTouchInput.hpp"

#include <game-activity/native_app_glue/android_native_app_glue.h>

#include <cmath>
#include <iterator>

namespace kb::game {

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
        } else if (action == AMOTION_EVENT_ACTION_CANCEL) {
            phase = input::InputTouchPhase::Cancelled;
        } else if (index == actionIndex &&
            (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_POINTER_UP)) {
            phase = input::InputTouchPhase::Ended;
        }
        const auto& point = event.pointers[index];
        frame_.Update(static_cast<std::uint32_t>(point.id), GameActivityPointerAxes_getX(&point),
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

} // namespace kb::game
