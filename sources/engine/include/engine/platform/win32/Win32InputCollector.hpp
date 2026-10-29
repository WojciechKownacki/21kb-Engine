#pragma once

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "engine/input/InputTouchPoint.hpp"
#include "engine/input/InputText.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace kb::scene { class Scene; }

namespace kb::input {

class InputDeviceState;

// Fills an engine InputDeviceState from the Win32 keyboard/mouse and gamepad
// devices owned by the foreground process. The collector is shared by every
// Win32 host (editor and standalone player), so platform polling has one
// production implementation.
class Win32InputCollector {
public:
    // Observes message-driven device input that cannot be polled: touch
    // contacts, raw relative mouse motion and mouse-wheel deltas. Touch messages are consumed (true);
    // wheel messages return false so the host UI can route the same wheel
    // event while the normalized runtime snapshot receives it next Collect.
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
    void Collect(kb::scene::Scene& scene, HWND ownerWindow);
    // Also used by deterministic hosts that supply the device snapshot themselves.
    void ApplyTextClipboard(kb::scene::Scene& scene, HWND ownerWindow);

private:
    void UpdateTouchPoint(
        std::uint32_t id, float x, float y, InputTouchPhase phase) noexcept;
    void AdvanceTouchFrame() noexcept;

    static constexpr std::size_t kMaxTouchPoints = 10U;
    bool hasPreviousMouse_ = false;
    bool copyWasDown_ = false;
    bool cutWasDown_ = false;
    bool pasteWasDown_ = false;
    POINT previousMouse_{};
    std::array<InputTouchPoint, kMaxTouchPoints> touchPoints_{};
    std::size_t touchPointCount_ = 0U;
    std::array<char32_t, 64U> pendingTextInput_{};
    std::size_t pendingTextInputCount_ = 0U;
    Utf16InputDecoder textDecoder_{};
    float pendingMouseWheel_ = 0.0F;
    float pendingRawMouseX_ = 0.0F;
    float pendingRawMouseY_ = 0.0F;
    bool rawRelativeMouse_ = false;
    HWND pointerCoordinateWindow_ = nullptr;
    RECT pointerClientViewport_{};
    std::uint32_t pointerRenderWidth_ = 0U;
    std::uint32_t pointerRenderHeight_ = 0U;
};

} // namespace kb::input

#endif
