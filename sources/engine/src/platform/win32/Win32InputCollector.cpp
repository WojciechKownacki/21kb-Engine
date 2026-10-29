#include "engine/platform/win32/Win32InputCollector.hpp"

#if defined(_WIN32)

#include "engine/input/InputDeviceState.hpp"
#include "engine/input/InputKey.hpp"
#include "engine/platform/win32/Win32InputKeyMap.hpp"
#include "engine/platform/win32/Win32GamepadInput.hpp"

#include <Xinput.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace kb::input {
namespace {

[[nodiscard]] bool KeyDown(int virtualKey) noexcept {
    return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

[[nodiscard]] bool OwnerProcessIsForeground(HWND ownerWindow) noexcept {
    const HWND foreground = GetForegroundWindow();
    if (foreground == nullptr || ownerWindow == nullptr) {
        return false;
    }
    // Accept any window owned by this process, including editor floating panels.
    DWORD foregroundProcess = 0U;
    static_cast<void>(GetWindowThreadProcessId(foreground, &foregroundProcess));
    return foregroundProcess == GetCurrentProcessId();
}


} // namespace

void Win32InputCollector::UpdateTouchPoint(
    std::uint32_t id, float x, float y, InputTouchPhase phase) noexcept {
    for (std::size_t index = 0U; index < touchPointCount_; ++index) {
        if (touchPoints_[index].id == id) {
            touchPoints_[index] = InputTouchPoint{.id = id, .x = x, .y = y, .phase = phase};
            return;
        }
    }
    if (touchPointCount_ < touchPoints_.size()) {
        touchPoints_[touchPointCount_++] =
            InputTouchPoint{.id = id, .x = x, .y = y, .phase = phase};
    }
}

void Win32InputCollector::AdvanceTouchFrame() noexcept {
    std::size_t writeIndex = 0U;
    for (std::size_t readIndex = 0U; readIndex < touchPointCount_; ++readIndex) {
        InputTouchPoint point = touchPoints_[readIndex];
        if (IsTouchTerminal(point.phase)) {
            continue;
        }
        if (point.phase == InputTouchPhase::Began) {
            point.phase = InputTouchPhase::Moved;
        }
        touchPoints_[writeIndex++] = point;
    }
    touchPointCount_ = writeIndex;
}

bool Win32InputCollector::HandleWindowMessage(
    HWND ownerWindow, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    if (message == WM_CANCELMODE) {
        for (std::size_t index = 0U; index < touchPointCount_; ++index) touchPoints_[index].phase = InputTouchPhase::Cancelled;
        return false;
    }
    if (message == WM_CHAR) {
        const std::uint16_t repeatCount =
            (std::max)(static_cast<std::uint16_t>(LOWORD(lparam)), std::uint16_t{1U});
        for (std::uint16_t repeat = 0U; repeat < repeatCount; ++repeat) {
            char32_t codePoint = 0U;
            if (textDecoder_.Consume(static_cast<char16_t>(wparam), codePoint) &&
                pendingTextInputCount_ < pendingTextInput_.size()) {
                pendingTextInput_[pendingTextInputCount_++] = codePoint;
            }
        }
        return false;
    }
    if (message == WM_MOUSEWHEEL) {
        pendingMouseWheel_ += static_cast<float>(
            static_cast<short>(HIWORD(wparam))) / static_cast<float>(WHEEL_DELTA);
        return false;
    }

    if (message == WM_POINTERDOWN || message == WM_POINTERUPDATE || message == WM_POINTERUP) {
        const UINT32 pointerId = GET_POINTERID_WPARAM(wparam);
        POINTER_TOUCH_INFO touchInfo{};
        if (GetPointerTouchInfo(pointerId, &touchInfo) == 0) {
            return false;
        }
        POINT client = touchInfo.pointerInfo.ptPixelLocation;
        if (ownerWindow == nullptr || ScreenToClient(ownerWindow, &client) == 0) {
            return false;
        }
        const InputTouchPhase phase = (touchInfo.pointerInfo.pointerFlags & POINTER_FLAG_CANCELED) != 0U
            ? InputTouchPhase::Cancelled : message == WM_POINTERDOWN
            ? InputTouchPhase::Began
            : (message == WM_POINTERUP ? InputTouchPhase::Ended : InputTouchPhase::Moved);
        UpdateTouchPoint(
            pointerId, static_cast<float>(client.x), static_cast<float>(client.y), phase);
        return true;
    }

    if (message != WM_TOUCH) {
        return false;
    }

    const UINT count = LOWORD(wparam);
    std::vector<TOUCHINPUT> touches(count);
    const HTOUCHINPUT handle = reinterpret_cast<HTOUCHINPUT>(lparam);
    const bool read = count > 0U &&
        GetTouchInputInfo(handle, count, touches.data(), sizeof(TOUCHINPUT)) != 0;
    if (read) {
        for (const TOUCHINPUT& touch : touches) {
            POINT client{
                TOUCH_COORD_TO_PIXEL(touch.x),
                TOUCH_COORD_TO_PIXEL(touch.y),
            };
            if (ownerWindow == nullptr || ScreenToClient(ownerWindow, &client) == 0) {
                continue;
            }
            const InputTouchPhase phase = (touch.dwFlags & TOUCHEVENTF_UP) != 0U
                ? InputTouchPhase::Ended
                : ((touch.dwFlags & TOUCHEVENTF_DOWN) != 0U
                    ? InputTouchPhase::Began
                    : InputTouchPhase::Moved);
            UpdateTouchPoint(
                touch.dwID,
                static_cast<float>(client.x),
                static_cast<float>(client.y),
                phase);
        }
    }
    static_cast<void>(CloseTouchInputHandle(handle));
    return true;
}

void Win32InputCollector::ConfigurePointerViewport(
    HWND coordinateWindow,
    const RECT& clientViewport,
    std::uint32_t renderWidth,
    std::uint32_t renderHeight) noexcept {
    pointerCoordinateWindow_ = coordinateWindow;
    pointerClientViewport_ = clientViewport;
    pointerRenderWidth_ = renderWidth;
    pointerRenderHeight_ = renderHeight;
}

void Win32InputCollector::ClearPointerViewport() noexcept {
    pointerCoordinateWindow_ = nullptr;
    pointerClientViewport_ = {};
    pointerRenderWidth_ = 0U;
    pointerRenderHeight_ = 0U;
}

void Win32InputCollector::Collect(InputDeviceState& state, HWND ownerWindow) noexcept {
    state.Reset();
    const bool focused = OwnerProcessIsForeground(ownerWindow);
    state.SetHasFocus(focused);
    CollectWin32Gamepads(state, ownerWindow);
    if (!focused) {
        hasPreviousMouse_ = false;
        touchPointCount_ = 0U;
        pendingMouseWheel_ = 0.0F;
        pendingTextInputCount_ = 0U;
        textDecoder_.Reset();
        return;
    }

    for (const Win32KeyBinding& binding : Win32InputKeyMap::KeyboardAndMouse()) {
        state.SetKeyDown(binding.key, KeyDown(binding.virtualKey));
    }

    const bool mappedViewport =
        pointerCoordinateWindow_ != nullptr &&
        pointerRenderWidth_ > 0U &&
        pointerRenderHeight_ > 0U &&
        pointerClientViewport_.right > pointerClientViewport_.left &&
        pointerClientViewport_.bottom > pointerClientViewport_.top;
    const float pointerDeltaScaleX = mappedViewport
        ? static_cast<float>(pointerRenderWidth_) /
            static_cast<float>(pointerClientViewport_.right - pointerClientViewport_.left)
        : 1.0F;
    const float pointerDeltaScaleY = mappedViewport
        ? static_cast<float>(pointerRenderHeight_) /
            static_cast<float>(pointerClientViewport_.bottom - pointerClientViewport_.top)
        : 1.0F;

    POINT cursor{};
    if (GetCursorPos(&cursor) != 0) {
        if (hasPreviousMouse_) {
            state.SetAnalog(
                InputKey::MouseX,
                static_cast<float>(cursor.x - previousMouse_.x) * pointerDeltaScaleX);
            state.SetAnalog(
                InputKey::MouseY,
                static_cast<float>(cursor.y - previousMouse_.y) * pointerDeltaScaleY);
        }
        previousMouse_ = cursor;
        hasPreviousMouse_ = true;

        const HWND coordinateWindow =
            mappedViewport ? pointerCoordinateWindow_ : ownerWindow;
        POINT client = cursor;
        if (ScreenToClient(coordinateWindow, &client) != 0) {
            if (mappedViewport) {
                const float viewportWidth = static_cast<float>(
                    pointerClientViewport_.right - pointerClientViewport_.left);
                const float viewportHeight = static_cast<float>(
                    pointerClientViewport_.bottom - pointerClientViewport_.top);
                state.SetPointerPosition(
                    static_cast<float>(client.x - pointerClientViewport_.left) *
                        static_cast<float>(pointerRenderWidth_) / viewportWidth,
                    static_cast<float>(client.y - pointerClientViewport_.top) *
                        static_cast<float>(pointerRenderHeight_) / viewportHeight);
            } else {
                state.SetPointerPosition(
                    static_cast<float>(client.x), static_cast<float>(client.y));
            }
        }
    }

    state.SetTouchPoints(std::span<const InputTouchPoint>{
        touchPoints_.data(), touchPointCount_});
    static_cast<void>(state.AddTextInput(std::span<const char32_t>{
        pendingTextInput_.data(), pendingTextInputCount_}));
    pendingTextInputCount_ = 0U;
    if (mappedViewport) {
        state.SetPointerViewportExtent(pointerRenderWidth_, pointerRenderHeight_);
    } else {
        RECT clientRect{};
        if (ownerWindow != nullptr && GetClientRect(ownerWindow, &clientRect) != 0 &&
            clientRect.right > clientRect.left && clientRect.bottom > clientRect.top) {
            state.SetPointerViewportExtent(
                static_cast<std::uint32_t>(clientRect.right - clientRect.left),
                static_cast<std::uint32_t>(clientRect.bottom - clientRect.top));
        } else {
            state.SetPointerViewportExtent(0U, 0U);
        }
    }
    AdvanceTouchFrame();
    state.SetAnalog(InputKey::MouseWheel, pendingMouseWheel_);
    pendingMouseWheel_ = 0.0F;
}

} // namespace kb::input

#endif
