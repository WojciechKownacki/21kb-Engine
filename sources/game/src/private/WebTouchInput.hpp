#pragma once

#include "TouchContactFrame.hpp"

#include <emscripten/html5.h>

namespace kb::game {

class WebTouchInput {
public:
    using StateResolver = input::InputDeviceState* (*)(void*) noexcept;
    using BeginHandler = void (*)(void*, float, float) noexcept;
    using FocusHandler = void (*)(void*, bool) noexcept;

    ~WebTouchInput() { Detach(); }
    WebTouchInput() = default;
    WebTouchInput(const WebTouchInput&) = delete;
    WebTouchInput& operator=(const WebTouchInput&) = delete;

    [[nodiscard]] bool Attach(const char* canvas, void* owner, StateResolver state,
        BeginHandler begin = nullptr, FocusHandler focus = nullptr) noexcept;
    void Detach() noexcept;
    void Suspend() noexcept;
    [[nodiscard]] bool Resume() noexcept;
    [[nodiscard]] bool Process(int type, const EmscriptenTouchEvent& event, input::InputDeviceState& state) noexcept;
    void SetViewport(std::uint32_t width, std::uint32_t height) noexcept;
    void FinishFrame() noexcept;

private:
    static bool OnTouch(int type, const EmscriptenTouchEvent* event, void* context) noexcept;
    static bool OnFocus(int type, const EmscriptenFocusEvent*, void* context) noexcept;
    void Publish() const noexcept;
    [[nodiscard]] bool RegisterCallbacks() noexcept;

    TouchContactFrame frame_;
    const char* canvas_ = nullptr;
    void* owner_ = nullptr;
    StateResolver state_ = nullptr;
    BeginHandler begin_ = nullptr;
    FocusHandler focus_ = nullptr;
    bool attached_ = false;
    bool suspended_ = false;
};

} // namespace kb::game
