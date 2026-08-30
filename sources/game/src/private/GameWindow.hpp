#pragma once

#include "kb/render/RenderSurface.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <string>

namespace kb::input {
class Win32InputCollector;
}

namespace kb::game {

// The game's only window: a plain resizable Win32 surface bgfx renders into.
// It owns no user interface of its own - it forwards device messages to the
// engine input collector and reports the two things the loop needs, a pending
// resize and the user's request to close.
class GameWindow final : public kb::render::RenderSurface {
public:
    GameWindow() = default;
    ~GameWindow() override;

    GameWindow(const GameWindow&) = delete;
    GameWindow& operator=(const GameWindow&) = delete;
    GameWindow(GameWindow&&) = delete;
    GameWindow& operator=(GameWindow&&) = delete;

    [[nodiscard]] bool Open(
        const std::wstring& title,
        std::uint32_t width,
        std::uint32_t height,
        kb::input::Win32InputCollector& inputCollector);

    [[nodiscard]] std::uint32_t Width() const noexcept override;
    [[nodiscard]] std::uint32_t Height() const noexcept override;
    [[nodiscard]] void* NativeWindowHandle() const noexcept override;
    [[nodiscard]] void* NativeDisplayHandle() const noexcept override;

    [[nodiscard]] HWND Handle() const noexcept { return window_; }
    [[nodiscard]] bool CloseRequested() const noexcept { return closeRequested_; }

    // Drains the queue. Returns false once the window has been closed.
    [[nodiscard]] bool PumpMessages() noexcept;
    // Reports a client-area change that has not been handed to the renderer yet.
    [[nodiscard]] bool ConsumeResize(std::uint32_t& width, std::uint32_t& height) noexcept;

private:
    static LRESULT CALLBACK WindowProc(
        HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept;

    static constexpr const wchar_t* kWindowClassName = L"21kbGameWindow";

    ATOM windowClass_ = 0U;
    HWND window_ = nullptr;
    kb::input::Win32InputCollector* inputCollector_ = nullptr;
    std::uint32_t width_ = 0U;
    std::uint32_t height_ = 0U;
    bool resizePending_ = false;
    bool closeRequested_ = false;
};

} // namespace kb::game
