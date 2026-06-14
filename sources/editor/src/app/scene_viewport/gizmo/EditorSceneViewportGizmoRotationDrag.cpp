#include "app/scene_viewport/gizmo/EditorSceneViewportGizmoRotationDrag.hpp"

#if defined(_WIN32)
#include <cmath>

namespace kb::editor {
namespace {

[[nodiscard]] kb::scene::Quat AxisAngle(int axis, float radians) noexcept {
    const float half = radians * 0.5F;
    const float s = std::sin(half);
    switch (axis) {
    case 0:
        return kb::scene::Quat{s, 0.0F, 0.0F, std::cos(half)};
    case 1:
        return kb::scene::Quat{0.0F, s, 0.0F, std::cos(half)};
    case 2:
        return kb::scene::Quat{0.0F, 0.0F, s, std::cos(half)};
    default:
        return kb::scene::Quat{};
    }
}

[[nodiscard]] float NormalizeAngle(float radians) noexcept {
    constexpr float kPi = 3.14159265358979323846F;
    constexpr float kTwoPi = 6.28318530717958647692F;
    while (radians > kPi) {
        radians -= kTwoPi;
    }
    while (radians < -kPi) {
        radians += kTwoPi;
    }
    return radians;
}

[[nodiscard]] float RotationDirectionForView(const EditorViewportCameraState& camera, int axis) noexcept {
    const float facing = EditorSceneViewportMath::Dot(EditorSceneViewportMath::AxisWorldDirection(axis), camera.Axes().forward);
    return facing >= 0.0F ? -1.0F : 1.0F;
}

} // namespace

float EditorSceneViewportGizmoRotationDrag::ScreenAngleFromCenter(
    const EditorViewportCameraState& camera,
    const RECT& renderArea,
    kb::scene::Vec3 targetPosition,
    float localX,
    float localY) noexcept {
    float centerX = 0.0F;
    float centerY = 0.0F;
    if (!EditorSceneViewportMath::WorldToScreen(camera, renderArea, targetPosition, centerX, centerY)) {
        return 0.0F;
    }
    return std::atan2(localY - centerY, localX - centerX);
}

bool EditorSceneViewportGizmoRotationDrag::Apply(
    EditorSceneContext& sceneContext,
    const EditorSceneViewportHit& hit,
    kb::scene::Vec3 dragStartTarget) {
    const EditorSceneGizmoState& gizmo = sceneContext.Gizmo();
    const EditorViewportCameraState& camera = sceneContext.ViewportCamera(hit.panelId);
    const float currentAngle = ScreenAngleFromCenter(camera, hit.renderArea, dragStartTarget, hit.localX, hit.localY);
    const float angleDelta = NormalizeAngle(currentAngle - gizmo.dragStartScreenAngle);
    const float radians = sceneContext.ViewportPreview(hit.panelId).SnapRotationRadians(
        angleDelta * RotationDirectionForView(camera, gizmo.draggedAxis));
    static_cast<void>(sceneContext.ApplyActiveTransformEditRotationDelta(AxisAngle(gizmo.draggedAxis, radians)));
    return true;
}

} // namespace kb::editor

#endif
