#pragma once

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "engine/input/InputDeviceState.hpp"
#include "engine/input/InputText.hpp"
#include "engine/input/InputTouchPoint.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace kb::input {

// Fills an engine InputDeviceState from the Win32 keyboard/mouse and XInput
// devices owned by the foreground process. The collector is shared by every
// Win32 host (editor and standalone player), so platform polling has one
// production implementation.
class Win32InputCollector {
public:
    // Observes message-driven device input that cannot be polled: committed
    // UTF-16 text, touch contacts and mouse-wheel deltas. Touch messages are
    // consumed. Text and wheel return false so an editor host can route the
    // same event while runtime receives it on the next Collect.
    [[nodiscard]] bool HandleWindowMessage(
        HWND ownerWindow, UINT message, WPARAM wparam, LPARAM lparam) noexcept;
    // Maps the OS cursor from `coordinateWindow` client pixels through the
    // visible viewport rectangle into the runtime render-target pixel extent.
    // Hosts with a full-window 1:1 viewport may leave this unconfigured.
    void ConfigurePointerViewport(
        HWND coordinateWindow,
        const RECT& clientViewport,
        std::uint32_t renderWidth,
        std::uint32_t renderHeight) noexcept;
    void ClearPointerViewport() noexcept;
    void Collect(InputDeviceState& state, HWND ownerWindow) noexcept;

private:
    [[nodiscard]] bool MapScreenPoint(
        HWND ownerWindow, POINT screenPoint, float& x, float& y) const noexcept;
    void UpdateTouchPoint(
        std::uint32_t id, float x, float y, InputTouchPhase phase) noexcept;
    void AdvanceTouchFrame() noexcept;

    static constexpr std::size_t kMaxTouchPoints = 10U;
    bool hasPreviousMouse_ = false;
    POINT previousMouse_{};
    std::array<InputTouchPoint, kMaxTouchPoints> touchPoints_{};
    std::size_t touchPointCount_ = 0U;
    std::array<char32_t, InputDeviceState::kMaxTextInputCodePoints> pendingTextInput_{};
    std::size_t pendingTextInputCount_ = 0U;
    Utf16InputDecoder textDecoder_{};
    float pendingMouseWheel_ = 0.0F;
    HWND pointerCoordinateWindow_ = nullptr;
    RECT pointerClientViewport_{};
    std::uint32_t pointerRenderWidth_ = 0U;
    std::uint32_t pointerRenderHeight_ = 0U;
};

} // namespace kb::input

#endif
