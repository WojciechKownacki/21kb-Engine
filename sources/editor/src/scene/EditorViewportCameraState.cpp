#include "scene/EditorViewportCameraState.hpp"

#include <algorithm>
#include <cmath>

namespace kb::editor {
namespace {

constexpr float kPi = 3.14159265358979323846F;
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

[[nodiscard]] float DegreesToRadians(float degrees) noexcept {
    return degrees * kPi / 180.0F;
}

[[nodiscard]] kb::scene::Vec3 Add(kb::scene::Vec3 a, kb::scene::Vec3 b) noexcept {
    return kb::scene::Vec3{ a.x + b.x, a.y + b.y, a.z + b.z };
}

[[nodiscard]] kb::scene::Vec3 Sub(kb::scene::Vec3 a, kb::scene::Vec3 b) noexcept {
    return kb::scene::Vec3{ a.x - b.x, a.y - b.y, a.z - b.z };
}

[[nodiscard]] kb::scene::Vec3 Mul(kb::scene::Vec3 value, float scale) noexcept {
    return kb::scene::Vec3{ value.x * scale, value.y * scale, value.z * scale };
}

[[nodiscard]] float Length(kb::scene::Vec3 value) noexcept {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

[[nodiscard]] kb::scene::Vec3 Normalize(kb::scene::Vec3 value) noexcept {
    const float length = Length(value);
    if (length <= 0.00001F) {
        return kb::scene::Vec3{};
    }
    return Mul(value, 1.0F / length);
}

[[nodiscard]] kb::scene::Vec3 Cross(kb::scene::Vec3 a, kb::scene::Vec3 b) noexcept {
    return kb::scene::Vec3{
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
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
    navigationMode_ = mode;
    lastX_ = x;
    lastY_ = y;
    if (mode == EditorViewportCameraNavigationMode::Orbit || mode == EditorViewportCameraNavigationMode::Dolly) {
        ResetOrbitPivot();
    }
}

bool EditorViewportCameraState::UpdatePointer(int x, int y) noexcept {
    if (!IsNavigating()) {
        lastX_ = x;
        lastY_ = y;
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

void EditorViewportCameraState::ClampPitch() noexcept {
    pitchDegrees_ = std::clamp(pitchDegrees_, kMinPitch, kMaxPitch);
}

void EditorViewportCameraState::MoveLocal(float right, float up, float forward) noexcept {
    const EditorViewportCameraAxes axes = Axes();
    position_ = Add(position_, Add(Mul(axes.right, right), Add(Mul(axes.up, up), Mul(axes.forward, forward))));
}

void EditorViewportCameraState::ResetOrbitPivot() noexcept {
    const EditorViewportCameraAxes axes = Axes();
    orbitPivot_ = Add(position_, Mul(axes.forward, orbitDistance_));
}

void EditorViewportCameraState::UpdateOrbitPosition() noexcept {
    const EditorViewportCameraAxes axes = Axes();
    position_ = Sub(orbitPivot_, Mul(axes.forward, orbitDistance_));
}

} // namespace kb::editor
