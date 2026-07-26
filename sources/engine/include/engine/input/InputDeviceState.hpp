#pragma once

#include "engine/input/InputKey.hpp"
#include "engine/input/InputTouchPoint.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace kb::input {

// A raw, platform-agnostic snapshot of every input device for a single frame.
//
// The platform layer (e.g. the editor's Win32/XInput collector) fills this each
// frame; the InputSubsystem consumes it during evaluation. It carries no Win32
// types so it can live in the cross-platform engine library.
//
// Normalized device model (LIB-116): keyboard and mouse are inherently singular
// (one of each per machine) and keep the original flat, single-instance storage.
// Gamepads are NOT singular - up to kMaxGamepads controllers can be connected at
// once - so every gamepad key additionally takes a gamepadIndex (default 0,
// preserving every pre-LIB-116 caller's exact behavior for "the" gamepad). Touch
// contacts have no fixed count or identity across frames, so they are not
// InputKeys at all: TouchPoints() exposes the raw per-contact list, while the
// single InputKey::TouchDown is a derived digital signal ("is anything touching
// the screen") usable through the same action-binding system as every other key.
class InputDeviceState {
public:
    static constexpr std::size_t kKeyCount = static_cast<std::size_t>(InputKey::Count);
    // Matches XInput's real hardware limit (XUSER_MAX_COUNT), not an arbitrary cap.
    static constexpr std::uint8_t kMaxGamepads = 4U;
    // Standard OS multi-touch contact limit (Win32 accepts up to this many points
    // per WM_TOUCH message on typical hardware); enough for real multi-touch
    // gestures without an unbounded allocation.
    static constexpr std::size_t kMaxTouchPoints = 10U;

    void Reset() noexcept {
        digital_.fill(false);
        analog_.fill(0.0F);
        for (auto& slot : extraGamepadDigital_) {
            slot.fill(false);
        }
        for (auto& slot : extraGamepadAnalog_) {
            slot.fill(0.0F);
        }
        touchPointCount_ = 0U;
    }

    // gamepadIndex is ignored for non-gamepad keys (keyboard/mouse/touch are
    // singular); out-of-range gamepad indices are dropped, not clamped, so a
    // platform bug can't silently alias controller input onto the wrong slot.
    void SetKeyDown(InputKey key, bool down, std::uint8_t gamepadIndex = 0U) noexcept {
        if (gamepadIndex == 0U || DeviceKindOf(key) != InputDeviceKind::Gamepad) {
            digital_[Index(key)] = down;
            return;
        }
        if (gamepadIndex < kMaxGamepads) {
            extraGamepadDigital_[gamepadIndex - 1U][GamepadRangeIndex(key)] = down;
        }
    }

    void SetAnalog(InputKey key, float value, std::uint8_t gamepadIndex = 0U) noexcept {
        if (gamepadIndex == 0U || DeviceKindOf(key) != InputDeviceKind::Gamepad) {
            analog_[Index(key)] = value;
            return;
        }
        if (gamepadIndex < kMaxGamepads) {
            extraGamepadAnalog_[gamepadIndex - 1U][GamepadRangeIndex(key)] = value;
        }
    }

    [[nodiscard]] bool IsKeyDown(InputKey key, std::uint8_t gamepadIndex = 0U) const noexcept {
        if (key == InputKey::TouchDown) {
            return HasActiveTouch();
        }
        if (gamepadIndex == 0U || DeviceKindOf(key) != InputDeviceKind::Gamepad) {
            return digital_[Index(key)];
        }
        return gamepadIndex < kMaxGamepads && extraGamepadDigital_[gamepadIndex - 1U][GamepadRangeIndex(key)];
    }

    // Returns the analog value for analog keys, or 1.0/0.0 for digital keys so
    // that a single accessor works for both during action evaluation.
    [[nodiscard]] float GetValue(InputKey key, std::uint8_t gamepadIndex = 0U) const noexcept {
        if (key == InputKey::TouchDown) {
            return HasActiveTouch() ? 1.0F : 0.0F;
        }
        if (gamepadIndex == 0U || DeviceKindOf(key) != InputDeviceKind::Gamepad) {
            if (IsAnalogKey(key)) {
                return analog_[Index(key)];
            }
            return digital_[Index(key)] ? 1.0F : 0.0F;
        }
        if (gamepadIndex >= kMaxGamepads) {
            return 0.0F;
        }
        if (IsAnalogKey(key)) {
            return extraGamepadAnalog_[gamepadIndex - 1U][GamepadRangeIndex(key)];
        }
        return extraGamepadDigital_[gamepadIndex - 1U][GamepadRangeIndex(key)] ? 1.0F : 0.0F;
    }

    // Replaces the touch contact list wholesale for this frame (the platform
    // collector re-derives the full active set each poll, mirroring how digital_
    // is filled fresh rather than diffed). Points beyond kMaxTouchPoints are
    // dropped, not silently overwriting an earlier one.
    void SetTouchPoints(std::span<const InputTouchPoint> points) noexcept {
        touchPointCount_ = std::min(points.size(), kMaxTouchPoints);
        for (std::size_t index = 0U; index < touchPointCount_; ++index) {
            touchPoints_[index] = points[index];
        }
    }

    [[nodiscard]] std::span<const InputTouchPoint> TouchPoints() const noexcept {
        return std::span<const InputTouchPoint>{touchPoints_.data(), touchPointCount_};
    }

    // Absolute pointer position (LIB-117), in active render-viewport pixels
    // after the platform host's window/viewport mapping - NOT reset by Reset(),
    // so it keeps its last known value across the
    // Reset()-then-refill cycle every Collect() call does (the platform layer
    // re-sets it immediately after Reset() whenever it has a reading; unlike a
    // delta there is no "avoid a jump" reason to zero it first).
    void SetPointerPosition(float x, float y) noexcept {
        pointerX_ = x;
        pointerY_ = y;
    }

    [[nodiscard]] float PointerX() const noexcept {
        return pointerX_;
    }

    [[nodiscard]] float PointerY() const noexcept {
        return pointerY_;
    }

    // LIB-120: whether the host window currently has input focus (foreground,
    // not minimized to the background) - NOT reset by Reset(), same reasoning
    // as pointer position: the platform layer sets it unconditionally every
    // Collect() call, including to false, so there is nothing for Reset() to
    // clear first. Losing focus already zeroes every key/axis/touch point via
    // Reset() + the platform layer's early return; this flag exists so script/
    // gameplay code can distinguish "genuinely nothing is pressed" from "input
    // is suppressed because the window lost focus" and react (e.g. auto-pause).
    void SetHasFocus(bool focus) noexcept {
        hasFocus_ = focus;
    }

    [[nodiscard]] bool HasFocus() const noexcept {
        return hasFocus_;
    }

    // LIB-120: hardware presence, independent of whether the gamepad is
    // currently pressing anything - a disconnected controller and an idle one
    // both report all-zero button/axis values, so this is the only way to tell
    // them apart. NOT reset by Reset(): like pointer position, it keeps its
    // last known value except when the platform layer actually has a fresh
    // reading to report (e.g. only while the host window has focus, mirroring
    // when gamepad polling itself happens).
    void SetGamepadConnected(std::uint8_t gamepadIndex, bool connected) noexcept {
        if (gamepadIndex < kMaxGamepads) {
            gamepadConnected_[gamepadIndex] = connected;
        }
    }

    [[nodiscard]] bool IsGamepadConnected(std::uint8_t gamepadIndex) const noexcept {
        return gamepadIndex < kMaxGamepads && gamepadConnected_[gamepadIndex];
    }

private:
    [[nodiscard]] bool HasActiveTouch() const noexcept {
        for (std::size_t index = 0U; index < touchPointCount_; ++index) {
            if (touchPoints_[index].phase != InputTouchPhase::Ended) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] static std::size_t Index(InputKey key) noexcept {
        const auto raw = static_cast<std::size_t>(key);
        return raw < kKeyCount ? raw : 0U;
    }

    // 0-based offset within the gamepad key range for the extra-slot arrays.
    [[nodiscard]] static std::size_t GamepadRangeIndex(InputKey key) noexcept {
        constexpr auto first = static_cast<std::size_t>(InputKey::GamepadFaceBottom);
        const auto raw = static_cast<std::size_t>(key);
        return raw >= first && raw < first + kGamepadRangeCount ? raw - first : 0U;
    }

    static constexpr std::size_t kGamepadRangeCount =
        static_cast<std::size_t>(InputKey::TouchDown) - static_cast<std::size_t>(InputKey::GamepadFaceBottom);

    std::array<bool, kKeyCount> digital_{};
    std::array<float, kKeyCount> analog_{};
    // Slots for gamepad index 1..(kMaxGamepads-1); index 0 uses digital_/analog_
    // above (same storage single-gamepad callers always used, zero behavior change).
    std::array<std::array<bool, kGamepadRangeCount>, kMaxGamepads - 1U> extraGamepadDigital_{};
    std::array<std::array<float, kGamepadRangeCount>, kMaxGamepads - 1U> extraGamepadAnalog_{};

    std::array<InputTouchPoint, kMaxTouchPoints> touchPoints_{};
    std::size_t touchPointCount_ = 0U;

    float pointerX_ = 0.0F;
    float pointerY_ = 0.0F;

    bool hasFocus_ = false;
    std::array<bool, kMaxGamepads> gamepadConnected_{};
};

} // namespace kb::input
