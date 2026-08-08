#include "scene/EditorViewportCameraState.hpp"

#include "engine/math/EngineMath.hpp"

#include <algorithm>
#include <cmath>

namespace kb::editor {
namespace {

// LIB-042/LIB-043: kb::scene::Vec3 is now an alias to kb::math::Vec3 (see
// TransformComponent.hpp), which already provides Length/Normalize/Cross —
// this file's own copies used to be a second definition and would now be
// an ambiguous overload via ADL against kb::math's. Sub/Mul have no
// kb::math equivalent yet, so they stay local.
using kb::math::Cross;
using kb::math::Length;
using kb::math::Normalize;

constexpr float kLookSensitivity = 0.12F;
constexpr float kLeftDollyScale = 0.045F;
constexpr float kPanScale = 0.018F;
constexpr float kOrbitDollyScale = 0.08F;
constexpr float kWheelDollyScale = 0.9F;
constexpr float kWheelSpeedScale = 1.18F;
constexpr float kMinPitch = -89.0F;
constexpr float kMaxPitch = 89.0F;
constexpr float kMinOrbitDistance = 0.15F;
constexpr float kMinSpeed = 0.05F;
constexpr float kMaxSpeed = 200.0F;

// LIB-044: delegates to the single canonical kb::math::ToRadians instead
// of an independently-rederived degrees-to-radians constant.
[[nodiscard]] float DegreesToRadians(float degrees) noexcept {
    return kb::math::ToRadians(kb::math::Degrees{ degrees }).Value();
}

[[nodiscard]] kb::scene::Vec3 Sub(kb::scene::Vec3 a, kb::scene::Vec3 b) noexcept {
    return kb::scene::Vec3{ a.x - b.x, a.y - b.y, a.z - b.z };
}

[[nodiscard]] kb::scene::Vec3 Mul(kb::scene::Vec3 value, float scale) noexcept {
    return kb::scene::Vec3{ value.x * scale, value.y * scale, value.z * scale };
}

[[nodiscard]] kb::scene::Vec3 Lerp(kb::scene::Vec3 a, kb::scene::Vec3 b, float t) noexcept {
    return kb::scene::Vec3{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
}

[[nodiscard]] float DirectionSign(bool positive, bool negative) noexcept {
    return (positive ? 1.0F : 0.0F) - (negative ? 1.0F : 0.0F);
}

} // namespace

const kb::scene::Vec3& EditorViewportCameraState::Position() const noexcept {
    return position_;
}

float EditorViewportCameraState::YawDegrees() const noexcept {
    return yawDegrees_;
}

float EditorViewportCameraState::PitchDegrees() const noexcept {
    return pitchDegrees_;
}

float EditorViewportCameraState::Speed() const noexcept {
    return speed_;
}

float EditorViewportCameraState::VerticalFovDegrees() const noexcept {
    return verticalFovDegrees_;
}

float EditorViewportCameraState::NearClip() const noexcept {
    return nearClip_;
}

float EditorViewportCameraState::FarClip() const noexcept {
    return farClip_;
}

EditorViewportCameraNavigationMode EditorViewportCameraState::NavigationMode() const noexcept {
    return navigationMode_;
}

bool EditorViewportCameraState::IsNavigating() const noexcept {
    return navigationMode_ != EditorViewportCameraNavigationMode::None;
}

bool EditorViewportCameraState::AllowsKeyboardFlight() const noexcept {
    return navigationMode_ == EditorViewportCameraNavigationMode::Look;
}

EditorViewportCameraAxes EditorViewportCameraState::Axes() const noexcept {
    const float yaw = DegreesToRadians(yawDegrees_);
    const float pitch = DegreesToRadians(pitchDegrees_);
    const float cosPitch = std::cos(pitch);
    const kb::scene::Vec3 forward = Normalize(kb::scene::Vec3{
        std::sin(yaw) * cosPitch,
        std::sin(pitch),
        std::cos(yaw) * cosPitch,
    });
    const kb::scene::Vec3 right = Normalize(kb::scene::Vec3{ std::cos(yaw), 0.0F, -std::sin(yaw) });
    const kb::scene::Vec3 up = Normalize(Cross(forward, right));
    return EditorViewportCameraAxes{
        .position = position_,
        .forward = forward,
        .right = right,
        .up = up,
    };
}

void EditorViewportCameraState::BeginNavigation(EditorViewportCameraNavigationMode mode, int x, int y) noexcept {
    focusAnimating_ = false;
    navigationMode_ = mode;
    lastX_ = x;
    lastY_ = y;
    pendingX_ = x;
    pendingY_ = y;
    hasPendingPointer_ = false;
    if (mode == EditorViewportCameraNavigationMode::Orbit || mode == EditorViewportCameraNavigationMode::Dolly) {
        ResetOrbitPivot();
    }
}

void EditorViewportCameraState::QueuePointer(int x, int y) noexcept {
    pendingX_ = x;
    pendingY_ = y;
    hasPendingPointer_ = true;
}

bool EditorViewportCameraState::ApplyQueuedPointer() noexcept {
    if (!hasPendingPointer_) {
        return false;
    }
    hasPendingPointer_ = false;
    return UpdatePointer(pendingX_, pendingY_);
}

bool EditorViewportCameraState::UpdatePointer(int x, int y) noexcept {
    if (!IsNavigating()) {
        lastX_ = x;
        lastY_ = y;
        pendingX_ = x;
        pendingY_ = y;
        hasPendingPointer_ = false;
        return false;
    }

    const int dx = x - lastX_;
    const int dy = y - lastY_;
    lastX_ = x;
    lastY_ = y;
    if (dx == 0 && dy == 0) {
        return false;
    }

    switch (navigationMode_) {
    case EditorViewportCameraNavigationMode::LeftYawDolly:
        yawDegrees_ += static_cast<float>(dx) * kLookSensitivity;
        MoveLocal(0.0F, 0.0F, static_cast<float>(dy) * kLeftDollyScale);
        return true;
    case EditorViewportCameraNavigationMode::Look:
        yawDegrees_ += static_cast<float>(dx) * kLookSensitivity;
        pitchDegrees_ -= static_cast<float>(dy) * kLookSensitivity;
        ClampPitch();
        return true;
    case EditorViewportCameraNavigationMode::Pan:
    case EditorViewportCameraNavigationMode::Track:
        MoveLocal(static_cast<float>(-dx) * kPanScale, static_cast<float>(dy) * kPanScale, 0.0F);
        return true;
    case EditorViewportCameraNavigationMode::Orbit:
        yawDegrees_ += static_cast<float>(dx) * kLookSensitivity;
        pitchDegrees_ -= static_cast<float>(dy) * kLookSensitivity;
        ClampPitch();
        UpdateOrbitPosition();
        return true;
    case EditorViewportCameraNavigationMode::Dolly:
        orbitDistance_ = std::max(kMinOrbitDistance, orbitDistance_ - static_cast<float>(dy) * kOrbitDollyScale);
        UpdateOrbitPosition();
        return true;
    case EditorViewportCameraNavigationMode::None:
        break;
    }
    return false;
}

void EditorViewportCameraState::EndNavigation() noexcept {
    navigationMode_ = EditorViewportCameraNavigationMode::None;
    hasPendingPointer_ = false;
}

bool EditorViewportCameraState::ApplyKeyboardFlight(const EditorViewportCameraFlightInput& input, float deltaSeconds) noexcept {
    if (!AllowsKeyboardFlight() || deltaSeconds <= 0.0F) {
        return false;
    }

    const float localForward = DirectionSign(input.forward, input.backward);
    const float localRight = DirectionSign(input.right, input.left);
    const float localUp = DirectionSign(input.up, input.down);
    if (localForward == 0.0F && localRight == 0.0F && localUp == 0.0F) {
        return false;
    }

    float multiplier = 1.0F;
    if (input.boost) {
        multiplier *= 4.0F;
    }
    if (input.slow) {
        multiplier *= 0.25F;
    }
    const float distance = speed_ * multiplier * deltaSeconds;
    MoveLocal(localRight * distance, localUp * distance, localForward * distance);
    return true;
}

bool EditorViewportCameraState::ApplyWheel(float wheelSteps, bool adjustSpeed) noexcept {
    if (wheelSteps == 0.0F) {
        return false;
    }

    if (adjustSpeed) {
        const float factor = std::pow(kWheelSpeedScale, wheelSteps);
        speed_ = std::clamp(speed_ * factor, kMinSpeed, kMaxSpeed);
        return true;
    }

    MoveLocal(0.0F, 0.0F, wheelSteps * kWheelDollyScale);
    return true;
}

void EditorViewportCameraState::SetViewAngles(float yawDegrees, float pitchDegrees) noexcept {
    focusAnimating_ = false;
    yawDegrees_ = yawDegrees;
    pitchDegrees_ = pitchDegrees;
    ClampPitch();
    UpdateOrbitPosition();
}

void EditorViewportCameraState::FocusOn(const kb::scene::Vec3& target, float radius, float durationSeconds) noexcept {
    const float safeRadius = std::max(0.25F, radius);
    const float halfFov = DegreesToRadians(std::clamp(verticalFovDegrees_, 1.0F, 179.0F) * 0.5F);
    const float tanHalf = std::max(0.05F, std::tan(halfFov));
    const EditorViewportCameraAxes axes = Axes();
    const kb::scene::Vec3 targetPivot = target;
    const float targetDistance = std::max(kMinOrbitDistance, (safeRadius / tanHalf) * 1.3F);
    const kb::scene::Vec3 targetPosition = Sub(targetPivot, Mul(axes.forward, targetDistance));
    if (durationSeconds <= 0.0F) {
        focusAnimating_ = false;
        orbitPivot_ = targetPivot;
        orbitDistance_ = targetDistance;
        position_ = targetPosition;
        return;
    }
    focusStartPosition_ = position_;
    focusStartPivot_ = orbitPivot_;
    focusStartDistance_ = orbitDistance_;
    focusTargetPosition_ = targetPosition;
    focusTargetPivot_ = targetPivot;
    focusTargetDistance_ = targetDistance;
    focusElapsed_ = 0.0F;
    focusDuration_ = durationSeconds;
    focusAnimating_ = true;
}

bool EditorViewportCameraState::TickFocus(float deltaSeconds) noexcept {
    if (!focusAnimating_) {
        return false;
    }
    // A manual navigation (the user grabbing the camera) takes precedence and
    // abandons the animation wherever it is.
    if (IsNavigating()) {
        focusAnimating_ = false;
        return false;
    }
    focusElapsed_ += std::max(0.0F, deltaSeconds);
    float t = focusDuration_ > 0.0F ? (focusElapsed_ / focusDuration_) : 1.0F;
    if (t >= 1.0F) {
        t = 1.0F;
        focusAnimating_ = false;
    }
    // Smoothstep easing: ease-in/ease-out so the move starts and settles gently.
    const float eased = t * t * (3.0F - 2.0F * t);
    position_ = Lerp(focusStartPosition_, focusTargetPosition_, eased);
    orbitPivot_ = Lerp(focusStartPivot_, focusTargetPivot_, eased);
    orbitDistance_ = focusStartDistance_ + (focusTargetDistance_ - focusStartDistance_) * eased;
    return true;
}

bool EditorViewportCameraState::IsFocusAnimating() const noexcept {
    return focusAnimating_;
}

void EditorViewportCameraState::ClampPitch() noexcept {
    pitchDegrees_ = std::clamp(pitchDegrees_, kMinPitch, kMaxPitch);
}

void EditorViewportCameraState::MoveLocal(float right, float up, float forward) noexcept {
    const EditorViewportCameraAxes axes = Axes();
    position_ = position_ + Mul(axes.right, right) + Mul(axes.up, up) + Mul(axes.forward, forward);
}

void EditorViewportCameraState::ResetOrbitPivot() noexcept {
    const EditorViewportCameraAxes axes = Axes();
    orbitPivot_ = position_ + Mul(axes.forward, orbitDistance_);
}

void EditorViewportCameraState::UpdateOrbitPosition() noexcept {
    const EditorViewportCameraAxes axes = Axes();
    position_ = Sub(orbitPivot_, Mul(axes.forward, orbitDistance_));
}

} // namespace kb::editor
