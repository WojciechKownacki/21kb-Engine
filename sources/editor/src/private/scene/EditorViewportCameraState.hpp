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
    [[nodiscard]] bool UpdatePointer(int x, int y) noexcept;
    void EndNavigation() noexcept;
    [[nodiscard]] bool ApplyKeyboardFlight(const EditorViewportCameraFlightInput& input, float deltaSeconds) noexcept;
    [[nodiscard]] bool ApplyWheel(float wheelSteps, bool adjustSpeed) noexcept;

private:
    void ClampPitch() noexcept;
    void MoveLocal(float right, float up, float forward) noexcept;
    void ResetOrbitPivot() noexcept;
    void UpdateOrbitPosition() noexcept;

    kb::scene::Vec3 position_{ 8.0F, 6.0F, -8.0F };
    kb::scene::Vec3 orbitPivot_{ 0.0F, 2.0F, 0.0F };
    float yawDegrees_ = -45.0F;
    float pitchDegrees_ = 30.0F;
    float orbitDistance_ = 6.0F;
    float speed_ = 6.0F;
    float verticalFovDegrees_ = 60.0F;
    float nearClip_ = 0.01F;
    float farClip_ = 1000.0F;
    EditorViewportCameraNavigationMode navigationMode_ = EditorViewportCameraNavigationMode::None;
    int lastX_ = 0;
    int lastY_ = 0;
};

} // namespace kb::editor
