#pragma once

#include "engine/math/DVec3.hpp"
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
    // In viewport space: relative to the camera's ViewportOrigin().
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

// The camera keeps its position in double precision. The viewport tools (picking rays, gizmo positions, overlays,
// the camera's own view) work in viewport space, relative to a viewport origin near the camera
// (docs/large_worlds.md), so they stay precise far from the world origin. The origin stays at the world origin until
// the camera is further than kViewportRebaseDistance from it along an axis, then moves to the camera's position
// rounded to kViewportGridStep.
class EditorViewportCameraState {
public:
    static constexpr double kViewportRebaseDistance = 1024.0;
    static constexpr double kViewportGridStep = 1024.0;

    // The world position, rounded to float.
    [[nodiscard]] kb::scene::Vec3 Position() const noexcept;
    [[nodiscard]] const kb::math::DVec3& PrecisePosition() const noexcept;
    // The world position viewport-space positions (Axes().position among them) are relative to.
    [[nodiscard]] const kb::math::DVec3& ViewportOrigin() const noexcept;
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
    // FocusOn a world-space target given in double precision.
    void FocusOn(const kb::math::DVec3& target, float radius, float durationSeconds) noexcept;
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
    // Moves the viewport origin to the camera once the camera is too far from it.
    void FollowViewportOrigin() noexcept;

    kb::math::DVec3 position_{ 8.0, 6.0, -8.0 };
    kb::math::DVec3 orbitPivot_{ 0.0, 2.0, 0.0 };
    kb::math::DVec3 viewportOrigin_{};
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
    kb::math::DVec3 focusStartPosition_{};
    kb::math::DVec3 focusStartPivot_{};
    float focusStartDistance_ = 0.0F;
    kb::math::DVec3 focusTargetPosition_{};
    kb::math::DVec3 focusTargetPivot_{};
    float focusTargetDistance_ = 0.0F;
};

} // namespace kb::editor
