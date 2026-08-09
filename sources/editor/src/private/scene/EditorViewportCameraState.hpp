#pragma once

#include "engine/scene/TransformComponent.hpp"

namespace kb::editor {

enum class EditorViewportCameraNavigationMode {
    None,
    LeftYawDolly,
    Look,
    Pan,
    Track,
    Orbit,
    Dolly,
};

struct EditorViewportCameraAxes {
    kb::scene::Vec3 position{};
    kb::scene::Vec3 forward{};
    kb::scene::Vec3 right{};
    kb::scene::Vec3 up{};
};

struct EditorViewportCameraFlightInput {
    bool forward = false;
    bool backward = false;
    bool right = false;
    bool left = false;
    bool up = false;
    bool down = false;
    bool boost = false;
    bool slow = false;
};

class EditorViewportCameraState {
public:
    [[nodiscard]] const kb::scene::Vec3& Position() const noexcept;
    [[nodiscard]] float YawDegrees() const noexcept;
    [[nodiscard]] float PitchDegrees() const noexcept;
    [[nodiscard]] float Speed() const noexcept;
    [[nodiscard]] float VerticalFovDegrees() const noexcept;
    [[nodiscard]] float NearClip() const noexcept;
    [[nodiscard]] float FarClip() const noexcept;
    [[nodiscard]] EditorViewportCameraNavigationMode NavigationMode() const noexcept;
    [[nodiscard]] bool IsNavigating() const noexcept;
    [[nodiscard]] bool AllowsKeyboardFlight() const noexcept;
    [[nodiscard]] EditorViewportCameraAxes Axes() const noexcept;

    void BeginNavigation(EditorViewportCameraNavigationMode mode, int x, int y) noexcept;
    void QueuePointer(int x, int y) noexcept;
    [[nodiscard]] bool ApplyQueuedPointer() noexcept;
    [[nodiscard]] bool UpdatePointer(int x, int y) noexcept;
    void EndNavigation() noexcept;
    [[nodiscard]] bool ApplyKeyboardFlight(const EditorViewportCameraFlightInput& input, float deltaSeconds) noexcept;
    [[nodiscard]] bool ApplyWheel(float wheelSteps, bool adjustSpeed) noexcept;
    void SetViewAngles(float yawDegrees, float pitchDegrees) noexcept;

    // Frames a world-space sphere (center, radius) in view while keeping the
    // current yaw/pitch: recenters the orbit pivot on the target and pulls the
    // camera back far enough for the sphere to fit the vertical FOV. With
    // durationSeconds <= 0 it snaps immediately; with a positive duration it
    // starts an eased animation from the current pose to the framed pose,
    // advanced by TickFocus. Drives the viewport "frame selected" (F) shortcut.
    void FocusOn(const kb::scene::Vec3& target, float radius, float durationSeconds) noexcept;
    // Advances an in-progress focus animation by deltaSeconds; returns true while
    // the animation is still running (so the caller keeps presenting), false when
    // nothing is animating. A manual camera navigation cancels the animation.
    [[nodiscard]] bool TickFocus(float deltaSeconds) noexcept;
    [[nodiscard]] bool IsFocusAnimating() const noexcept;

private:
    void ClampPitch() noexcept;
    void MoveLocal(float right, float up, float forward) noexcept;
    void ResetOrbitPivot() noexcept;
    void UpdateOrbitPosition() noexcept;

    kb::scene::Vec3 position_{ 8.0F, 6.0F, -8.0F };
    kb::scene::Vec3 orbitPivot_{ 0.0F, 2.0F, 0.0F };
    float yawDegrees_ = -45.0F;
    float pitchDegrees_ = -30.0F;
    float orbitDistance_ = 6.0F;
    float speed_ = 6.0F;
    float verticalFovDegrees_ = 60.0F;
    float nearClip_ = 0.01F;
    float farClip_ = 1000.0F;
    EditorViewportCameraNavigationMode navigationMode_ = EditorViewportCameraNavigationMode::None;
    int lastX_ = 0;
    int lastY_ = 0;
    int pendingX_ = 0;
    int pendingY_ = 0;
    bool hasPendingPointer_ = false;
    kb::scene::Vec3 flightVelocityLocal_{};

    bool focusAnimating_ = false;
    float focusElapsed_ = 0.0F;
    float focusDuration_ = 0.0F;
    kb::scene::Vec3 focusStartPosition_{};
    kb::scene::Vec3 focusStartPivot_{};
    float focusStartDistance_ = 0.0F;
    kb::scene::Vec3 focusTargetPosition_{};
    kb::scene::Vec3 focusTargetPivot_{};
    float focusTargetDistance_ = 0.0F;
};

} // namespace kb::editor
