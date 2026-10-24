#include "AndroidTouchInput.hpp"

#include <game-activity/native_app_glue/android_native_app_glue.h>

#include <cmath>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
GameActivityMotionEvent Down(int id = 7) {
    GameActivityMotionEvent event{};
    event.source = AINPUT_SOURCE_TOUCHSCREEN;
    event.action = AMOTION_EVENT_ACTION_DOWN;
    event.pointerCount = 1U;
    event.pointers[0].id = id;
    event.pointers[0].axisValues[AMOTION_EVENT_AXIS_X] = 20.0F;
    event.pointers[0].axisValues[AMOTION_EVENT_AXIS_Y] = 30.0F;
    return event;
}
} // namespace

void VerifyAndroidTouchInput() {
    using kb::input::InputTouchPhase;
    kb::game::AndroidTouchInput input;
    kb::input::InputDeviceState state;
    state.SetKeyDown(kb::input::InputKey::A, true);
    input.SetViewport(640U, 360U);
    auto event = Down();
    Require(input.Process(event), "Touch down must be accepted");
    const auto unchanged = [&] {
        input.Publish(state);
        const auto points = state.TouchPoints();
        Require(points.size() == 1U && points[0].id == 7U && points[0].x == 20.0F &&
            points[0].y == 30.0F && points[0].phase == InputTouchPhase::Began &&
            state.IsKeyDown(kb::input::InputKey::A), "Invalid touch event must not alter existing input");
    };
    event.pointerCount = static_cast<std::uint32_t>(std::size(event.pointers)) + 1U;
    Require(!input.Process(event), "Oversized native touch pointer array must be rejected"); unchanged();
    event = Down(); event.pointers[0].id = -1;
    Require(!input.Process(event), "Negative pointer ID must be rejected"); unchanged();
    event = Down(); event.pointers[0].axisValues[AMOTION_EVENT_AXIS_X] = std::numeric_limits<float>::quiet_NaN();
    Require(!input.Process(event), "Nonfinite touch coordinate must be rejected"); unchanged();
    event = Down(); event.pointerCount = 2U; event.pointers[1] = event.pointers[0];
    Require(!input.Process(event), "Duplicate IDs in a native event must be rejected"); unchanged();
    event = Down(); event.action = AMOTION_EVENT_ACTION_POINTER_UP | (1 << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
    Require(!input.Process(event), "Invalid action pointer index must be rejected"); unchanged();
    event = Down(); event.source = AINPUT_SOURCE_JOYSTICK;
    Require(!input.Process(event), "Controller motion must not become touch input"); unchanged();
    event = Down(); event.action = AMOTION_EVENT_ACTION_HOVER_MOVE;
    Require(!input.Process(event), "Hover must not become a contact"); unchanged();
    event = Down(); event.action = AMOTION_EVENT_ACTION_MOVE;
    event.pointers[0].axisValues[AMOTION_EVENT_AXIS_X] = -15.0F;
    Require(input.Process(event), "Finite coordinates outside the view must retain drag positions");
    input.Publish(state);
    Require(state.TouchPoints()[0].phase == InputTouchPhase::Began && state.TouchPoints()[0].x == -15.0F,
        "Queued motion must retain begin phase and latest coordinates");
    input.Cancel(); input.FinishFrame();
    for (const auto command : {APP_CMD_LOST_FOCUS, APP_CMD_PAUSE, APP_CMD_STOP, APP_CMD_TERM_WINDOW,
             APP_CMD_WINDOW_RESIZED, APP_CMD_CONFIG_CHANGED}) {
        input.FinishFrame(); event = Down(); Require(input.Process(event), "Lifecycle fixture down");
        input.HandleAppCommand(command); input.Publish(state);
        Require(state.TouchPoints().size() == 1U && state.TouchPoints()[0].phase == InputTouchPhase::Ended &&
            !state.IsKeyDown(kb::input::InputKey::TouchDown), "Lifecycle cancellation must expose a terminal contact");
        input.Publish(state);
        Require(state.TouchPoints().size() == 1U, "Cancellation must remain until a frame consumes it");
    }
    input.FinishFrame();
    event = Down(); event.action = AMOTION_EVENT_ACTION_MOVE;
    Require(input.Process(event), "Stale move is a valid native event"); input.Publish(state);
    Require(state.TouchPoints().empty(), "Move after cancellation must not resurrect a finger");
    event = Down(); Require(input.Process(event), "Viewport fixture down");
    input.SetViewport(360U, 640U); input.Publish(state);
    Require(state.PointerViewportWidth() == 360U && state.PointerViewportHeight() == 640U &&
        state.TouchPoints()[0].phase == InputTouchPhase::Ended, "Resize must publish the new viewport and cancel old contacts");
    input.FinishFrame(); event = Down(); Require(input.Process(event), "Cancel fixture down");
    event.pointerCount = 0U; event.action = AMOTION_EVENT_ACTION_CANCEL;
    Require(input.Process(event), "Empty system cancel must be accepted"); input.Publish(state);
    Require(state.TouchPoints()[0].phase == InputTouchPhase::Ended, "Empty system cancel must end known contacts");
    input.FinishFrame();
    event = Down(); Require(input.Process(event), "Reused ID fixture down");
    event.action = AMOTION_EVENT_ACTION_UP; Require(input.Process(event), "Reused ID fixture up");
    event.action = AMOTION_EVENT_ACTION_DOWN; Require(input.Process(event), "Reused ID fixture next down");
    input.Publish(state);
    Require(state.TouchPoints().size() == 2U && state.TouchPoints()[0].phase == InputTouchPhase::Ended &&
        state.TouchPoints()[1].phase == InputTouchPhase::Began && state.TouchPoints()[0].id == state.TouchPoints()[1].id,
        "Reusing an ID in one frame must preserve the old end and the new begin");
    input.Cancel(); input.FinishFrame();
    for (int id = 0; id < 12; ++id) { event = Down(id); Require(input.Process(event), "Bounded touch fixture"); }
    input.Publish(state);
    Require(state.TouchPoints().size() == kb::input::InputDeviceState::kMaxTouchPoints,
        "Pending contacts must remain bounded without overwriting previous IDs");
    for (std::size_t index = 0U; index < state.TouchPoints().size(); ++index) {
        Require(state.TouchPoints()[index].id == index, "Touch overflow must not alias existing IDs");
    }
}
