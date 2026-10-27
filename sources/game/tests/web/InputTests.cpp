#include "WebTouchInput.hpp"
#include "WebGamepadInput.hpp"
#include "DigitalInputFrame.hpp"

#include <emscripten/emscripten.h>

#include <cstddef>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>

namespace {
kb::input::InputDeviceState state;
kb::game::WebTouchInput input;
unsigned begins = 0U, mouseEvents = 0U, focusChanges = 0U;
bool attached = false;
std::string snapshot;

kb::input::InputDeviceState* Resolve(void*) noexcept { return &state; }
void Begin(void*, float, float) noexcept { ++begins; }
void Focus(void*, bool) noexcept { ++focusChanges; }
bool Mouse(int type, const EmscriptenMouseEvent*, void*) {
    ++mouseEvents;
    state.SetKeyDown(kb::input::InputKey::MouseLeft, type == EMSCRIPTEN_EVENT_MOUSEDOWN);
    return false;
}
} // namespace

extern "C" EMSCRIPTEN_KEEPALIVE const char* kb_input_snapshot() {
    std::ostringstream text;
    text << "{\"attached\":" << attached << ",\"asan\":" << __has_feature(address_sanitizer)
         << ",\"ubsan\":" << __has_feature(undefined_behavior_sanitizer) << ",\"focus\":" << state.HasFocus()
         << ",\"mouseEvents\":" << mouseEvents << ",\"begins\":" << begins << ",\"focusChanges\":" << focusChanges
         << ",\"mouseLeft\":" << state.IsKeyDown(kb::input::InputKey::MouseLeft)
         << ",\"touchDown\":" << state.IsKeyDown(kb::input::InputKey::TouchDown)
         << ",\"viewport\":[" << state.PointerViewportWidth() << ',' << state.PointerViewportHeight()
         << "],\"pointer\":[" << state.PointerX() << ',' << state.PointerY() << "],\"points\":[";
    bool first = true;
    for (const auto& point : state.TouchPoints()) {
        if (!first) text << ',';
        first = false;
        text << '[' << point.id << ',' << static_cast<unsigned>(point.phase) << ',' << point.x << ',' << point.y << ']';
    }
    text << "]}"; snapshot = text.str(); return snapshot.c_str();
}

extern "C" EMSCRIPTEN_KEEPALIVE void kb_input_finish() { input.FinishFrame(); }

extern "C" EMSCRIPTEN_KEEPALIVE int kb_digital_transitions() {
    using kb::input::InputKey;
    kb::input::InputDeviceState sample;
    kb::game::DigitalInputFrame frame;
    if (!frame.Queue(sample, InputKey::MouseLeft, true, true, 10.0F, 20.0F) ||
        !frame.Queue(sample, InputKey::MouseLeft, false, true, 11.0F, 21.0F)) return 1;
    frame.SetPointerPosition(100.0F, 200.0F);
    frame.Publish(sample);
    if (!sample.IsKeyDown(InputKey::MouseLeft) || sample.PointerX() != 10.0F) return 2;
    frame.Publish(sample);
    if (sample.IsKeyDown(InputKey::MouseLeft) || sample.PointerX() != 11.0F) return 3;
    frame.Publish(sample);
    if (sample.PointerX() != 100.0F || sample.PointerY() != 200.0F) return 4;
    for (int click = 0; click < 2; ++click) {
        if (!frame.Queue(sample, InputKey::MouseLeft, true) ||
            !frame.Queue(sample, InputKey::MouseLeft, true) ||
            !frame.Queue(sample, InputKey::MouseLeft, false)) return 5;
    }
    for (int edge = 0; edge < 4; ++edge) {
        frame.Publish(sample);
        if (sample.IsKeyDown(InputKey::MouseLeft) != (edge % 2 == 0)) return 6;
    }
    if (!frame.Queue(sample, InputKey::LeftShift, true) || !frame.Queue(sample, InputKey::A, true) ||
        !frame.Queue(sample, InputKey::A, false) || !frame.Queue(sample, InputKey::LeftShift, false)) return 7;
    frame.Publish(sample);
    if (!sample.IsKeyDown(InputKey::A) || !sample.IsKeyDown(InputKey::LeftShift)) return 8;
    frame.Publish(sample);
    if (sample.IsKeyDown(InputKey::A) || sample.IsKeyDown(InputKey::LeftShift)) return 9;
    if (!frame.Queue(sample, InputKey::A, true)) return 10;
    frame.Clear();
    sample.Reset();
    frame.Publish(sample);
    if (sample.IsKeyDown(InputKey::A)) return 11;
    for (std::size_t edge = 0; edge < frame.kCapacity; ++edge) {
        if (!frame.Queue(sample, InputKey::A, edge % 2U == 0U)) return 12;
    }
    sample.SetKeyDown(InputKey::MouseLeft, true);
    sample.SetKeyDown(InputKey::GamepadFaceBottom, true);
    if (frame.Queue(sample, InputKey::A, true)) return 13;
    frame.Publish(sample);
    if (sample.IsKeyDown(InputKey::A) || sample.IsKeyDown(InputKey::MouseLeft) ||
        !sample.IsKeyDown(InputKey::GamepadFaceBottom)) return 14;
    if (frame.Queue(sample, InputKey::Count, true) || frame.Queue(sample, InputKey::None, true) ||
        frame.Queue(sample, InputKey::GamepadFaceBottom, true)) return 15;
    return 0;
}

extern "C" EMSCRIPTEN_KEEPALIVE const char* kb_gamepad_snapshot() {
    kb::game::PollWebGamepads(state);
    std::ostringstream text;
    text << '[';
    for (std::uint8_t slot = 0U; slot < state.kMaxGamepads; ++slot) {
        if (slot != 0U) text << ',';
        using kb::input::InputKey;
        text << '[' << state.IsGamepadConnected(slot) << ','
             << state.GetValue(InputKey::GamepadFaceBottom, slot) << ','
             << state.GetValue(InputKey::GamepadFaceRight, slot) << ','
             << state.GetValue(InputKey::GamepadLeftStickX, slot) << ','
             << state.GetValue(InputKey::GamepadLeftStickY, slot) << ','
             << state.GetValue(InputKey::GamepadRightStickX, slot) << ','
             << state.GetValue(InputKey::GamepadRightStickY, slot) << ','
             << state.GetValue(InputKey::GamepadLeftTrigger, slot) << ','
             << state.GetValue(InputKey::GamepadRightTrigger, slot) << ']';
    }
    text << ']'; snapshot = text.str(); return snapshot.c_str();
}

extern "C" EMSCRIPTEN_KEEPALIVE int kb_gamepad_invalid() {
    using kb::input::InputKey;
    kb::input::InputDeviceState sampleState;
    EmscriptenGamepadEvent event{};
    event.connected = true;
    std::memcpy(event.mapping, "standard", 9U);
    event.numAxes = 4;
    event.numButtons = 17;
    event.axis[0] = std::numeric_limits<double>::infinity();
    event.axis[1] = std::numeric_limits<double>::quiet_NaN();
    event.axis[2] = 10.0;
    event.axis[3] = -10.0;
    event.analogButton[6] = -2.0;
    event.analogButton[7] = 2.0;
    event.digitalButton[15] = true;
    kb::game::ApplyWebGamepadSample(sampleState, 3U, &event);
    if (!sampleState.IsGamepadConnected(3U) || sampleState.IsGamepadConnected(0U) ||
        sampleState.GetValue(InputKey::GamepadLeftStickX, 3U) != 0.0F ||
        sampleState.GetValue(InputKey::GamepadLeftStickY, 3U) != 0.0F ||
        sampleState.GetValue(InputKey::GamepadRightStickX, 3U) != 1.0F ||
        sampleState.GetValue(InputKey::GamepadRightStickY, 3U) != 1.0F ||
        sampleState.GetValue(InputKey::GamepadLeftTrigger, 3U) != 0.0F ||
        sampleState.GetValue(InputKey::GamepadRightTrigger, 3U) != 1.0F ||
        !sampleState.IsKeyDown(InputKey::GamepadDPadRight, 3U)) return 0;
    for (int invalid : {-1, 65}) {
        event.numButtons = invalid;
        kb::game::ApplyWebGamepadSample(sampleState, 3U, &event);
        if (sampleState.GetValue(InputKey::GamepadRightStickX, 3U) != 0.0F ||
            sampleState.IsKeyDown(InputKey::GamepadDPadRight, 3U)) return 0;
    }
    event.numButtons = 17;
    event.numAxes = 65;
    kb::game::ApplyWebGamepadSample(sampleState, 3U, &event);
    if (sampleState.IsKeyDown(InputKey::GamepadDPadRight, 3U)) return 0;
    event.numAxes = 4;
    kb::game::ApplyWebGamepadSample(sampleState, 3U, &event);
    kb::game::ApplyWebGamepadSample(sampleState, 4U, nullptr);
    if (!sampleState.IsKeyDown(InputKey::GamepadDPadRight, 3U)) return 0;
    kb::game::ApplyWebGamepadSample(sampleState, 3U, nullptr);
    return !sampleState.IsGamepadConnected(3U) &&
        !sampleState.IsKeyDown(InputKey::GamepadDPadRight, 3U) &&
        sampleState.GetValue(InputKey::GamepadRightTrigger, 3U) == 0.0F ? 1 : 0;
}

extern "C" EMSCRIPTEN_KEEPALIVE void kb_input_resize() {
    int width = 0, height = 0;
    if (emscripten_get_canvas_element_size("#canvas", &width, &height) == EMSCRIPTEN_RESULT_SUCCESS && width > 0 && height > 0) {
        input.SetViewport(static_cast<unsigned>(width), static_cast<unsigned>(height));
    }
}

extern "C" EMSCRIPTEN_KEEPALIVE void kb_input_detach() { input.Detach(); attached = false; }

extern "C" EMSCRIPTEN_KEEPALIVE int kb_input_invalid() {
    const std::string before{kb_input_snapshot()};
    EmscriptenTouchEvent event{};
    event.numTouches = -1;
    if (input.Process(EMSCRIPTEN_EVENT_TOUCHSTART, event, state)) return 0;
    event.numTouches = 33;
    if (input.Process(EMSCRIPTEN_EVENT_TOUCHSTART, event, state)) return 0;
    event.numTouches = 1; event.touches[0].identifier = -1;
    if (input.Process(EMSCRIPTEN_EVENT_TOUCHSTART, event, state)) return 0;
    event.touches[0].identifier = 7; event.numTouches = 2; event.touches[1] = event.touches[0];
    if (input.Process(EMSCRIPTEN_EVENT_TOUCHSTART, event, state)) return 0;
    event.numTouches = 0;
    if (input.Process(EMSCRIPTEN_EVENT_TOUCHSTART, event, state)) return 0;
    if (input.Process(EMSCRIPTEN_EVENT_KEYDOWN, event, state)) return 0;
    return before == kb_input_snapshot() ? 1 : 0;
}

int main() {
    attached = input.Attach("#canvas", nullptr, &Resolve, &Begin, &Focus);
    if (!attached) return 1;
    kb_input_resize();
    static_cast<void>(emscripten_set_mousedown_callback("#canvas", nullptr, false, &Mouse));
    static_cast<void>(emscripten_set_mouseup_callback("#canvas", nullptr, false, &Mouse));
    EM_ASM({ document.documentElement.dataset.kbInputReady = 'true'; });
    emscripten_exit_with_live_runtime();
}
