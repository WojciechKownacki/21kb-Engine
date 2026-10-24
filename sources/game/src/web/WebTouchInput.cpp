#include "WebTouchInput.hpp"

#include <emscripten/emscripten.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>

namespace {
// clang-format off
EM_JS(void, SetTouchAction, (const char* selector, bool enabled), {
    const canvas = document.querySelector(UTF8ToString(selector));
    if (!canvas) return;
    if (enabled) {
        canvas.kbOriginalTouchAction = canvas.style.touchAction;
        canvas.style.touchAction = 'none';
    } else if (Object.prototype.hasOwnProperty.call(canvas, 'kbOriginalTouchAction')) {
        canvas.style.touchAction = canvas.kbOriginalTouchAction || "";
        delete canvas.kbOriginalTouchAction;
    }
});
// clang-format on
} // namespace

namespace kb::game {

bool WebTouchInput::Attach(const char* canvas, void* owner, StateResolver state, BeginHandler begin, FocusHandler focus) noexcept {
    Detach();
    if (canvas == nullptr || state == nullptr) return false;
    canvas_ = canvas;
    owner_ = owner;
    state_ = state;
    begin_ = begin;
    focus_ = focus;
    frame_ = TouchContactFrame{};
    attached_ = true;
    const std::array results{
        emscripten_set_touchstart_callback(canvas_, this, true, &OnTouch),
        emscripten_set_touchmove_callback(canvas_, this, true, &OnTouch),
        emscripten_set_touchend_callback(canvas_, this, true, &OnTouch),
        emscripten_set_touchcancel_callback(canvas_, this, true, &OnTouch),
        emscripten_set_focus_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, false, &OnFocus),
        emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, false, &OnFocus),
    };
    if (!std::ranges::all_of(results, [](auto result) { return result == EMSCRIPTEN_RESULT_SUCCESS; })) {
        Detach();
        return false;
    }
    SetTouchAction(canvas_, true);
    if (auto* input = state_(owner_)) input->SetHasFocus(EM_ASM_INT({ return document.hasFocus() ? 1 : 0; }) != 0);
    return true;
}

void WebTouchInput::Detach() noexcept {
    if (!attached_) return;
    attached_ = false;
    static_cast<void>(emscripten_set_touchstart_callback(canvas_, nullptr, true, nullptr));
    static_cast<void>(emscripten_set_touchmove_callback(canvas_, nullptr, true, nullptr));
    static_cast<void>(emscripten_set_touchend_callback(canvas_, nullptr, true, nullptr));
    static_cast<void>(emscripten_set_touchcancel_callback(canvas_, nullptr, true, nullptr));
    static_cast<void>(emscripten_set_focus_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, false, nullptr));
    static_cast<void>(emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, false, nullptr));
    SetTouchAction(canvas_, false);
    frame_.Cancel();
    Publish();
    state_ = nullptr;
    owner_ = nullptr;
}

bool WebTouchInput::Process(int type, const EmscriptenTouchEvent& event, input::InputDeviceState& state) noexcept {
    if (type != EMSCRIPTEN_EVENT_TOUCHSTART && type != EMSCRIPTEN_EVENT_TOUCHMOVE &&
        type != EMSCRIPTEN_EVENT_TOUCHEND && type != EMSCRIPTEN_EVENT_TOUCHCANCEL) return false;
    if (event.numTouches < 0 || event.numTouches > static_cast<int>(std::size(event.touches)) ||
        (event.numTouches == 0 && type != EMSCRIPTEN_EVENT_TOUCHCANCEL) || canvas_ == nullptr) return false;
    for (int index = 0; index < event.numTouches; ++index) {
        if (event.touches[index].identifier < 0) return false;
        for (int earlier = 0; earlier < index; ++earlier) {
            if (event.touches[index].identifier == event.touches[earlier].identifier) return false;
        }
    }
    int width = 0, height = 0;
    double cssWidth = 0.0, cssHeight = 0.0;
    if (emscripten_get_canvas_element_size(canvas_, &width, &height) != EMSCRIPTEN_RESULT_SUCCESS ||
        emscripten_get_element_css_size(canvas_, &cssWidth, &cssHeight) != EMSCRIPTEN_RESULT_SUCCESS ||
        width <= 0 || height <= 0 || !std::isfinite(cssWidth) || !std::isfinite(cssHeight) || cssWidth <= 0.0 || cssHeight <= 0.0) return false;
    std::array<std::array<float, 2U>, 32U> positions{};
    for (int index = 0; index < event.numTouches; ++index) {
        const double x = static_cast<double>(event.touches[index].targetX) * width / cssWidth;
        const double y = static_cast<double>(event.touches[index].targetY) * height / cssHeight;
        if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > std::numeric_limits<float>::max() ||
            std::abs(y) > std::numeric_limits<float>::max()) return false;
        positions[static_cast<std::size_t>(index)] = {static_cast<float>(x), static_cast<float>(y)};
    }
    frame_.SetViewport(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
    if (type == EMSCRIPTEN_EVENT_TOUCHCANCEL && event.numTouches == 0) frame_.Cancel();
    for (int index = 0; index < event.numTouches; ++index) {
        const auto& touch = event.touches[index];
        const auto id = static_cast<std::uint32_t>(touch.identifier);
        if (!touch.onTarget && !frame_.Contains(id)) continue;
        auto phase = input::InputTouchPhase::Moved;
        if (touch.isChanged && type == EMSCRIPTEN_EVENT_TOUCHSTART) phase = input::InputTouchPhase::Began;
        else if (touch.isChanged && type == EMSCRIPTEN_EVENT_TOUCHEND) phase = input::InputTouchPhase::Ended;
        else if (touch.isChanged && type == EMSCRIPTEN_EVENT_TOUCHCANCEL) phase = input::InputTouchPhase::Cancelled;
        const auto& position = positions[static_cast<std::size_t>(index)];
        frame_.Update(id, position[0], position[1], phase);
        if (phase == input::InputTouchPhase::Began && begin_ != nullptr && frame_.Contains(id)) begin_(owner_, position[0], position[1]);
    }
    frame_.Publish(state);
    return true;
}

bool WebTouchInput::OnTouch(int type, const EmscriptenTouchEvent* event, void* context) noexcept {
    auto* input = static_cast<WebTouchInput*>(context);
    if (input == nullptr || event == nullptr || input->state_ == nullptr) return false;
    auto* state = input->state_(input->owner_);
    return state != nullptr && state->HasFocus() && input->Process(type, *event, *state);
}

bool WebTouchInput::OnFocus(int type, const EmscriptenFocusEvent*, void* context) noexcept {
    auto* input = static_cast<WebTouchInput*>(context);
    if (input == nullptr || input->state_ == nullptr) return false;
    const bool focused = type == EMSCRIPTEN_EVENT_FOCUS;
    if (auto* state = input->state_(input->owner_)) {
        if (!focused) { state->Reset(); input->frame_.Cancel(); }
        state->SetHasFocus(focused);
        input->frame_.Publish(*state);
    }
    if (input->focus_ != nullptr) input->focus_(input->owner_, focused);
    return false;
}

void WebTouchInput::Publish() const noexcept {
    if (state_ != nullptr) {
        if (auto* state = state_(owner_)) frame_.Publish(*state);
    }
}

void WebTouchInput::SetViewport(std::uint32_t width, std::uint32_t height) noexcept {
    frame_.SetViewport(width, height);
    Publish();
}

void WebTouchInput::FinishFrame() noexcept {
    frame_.FinishFrame();
    Publish();
}

} // namespace kb::game
