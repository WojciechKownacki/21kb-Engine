#include "app/scene_viewport/EditorSceneViewportGizmoHitTester.hpp"

#if defined(_WIN32)
#include <algorithm>
#include <cmath>

namespace kb::editor {
namespace {

constexpr float kGizmoAxisLength = 1.16F;
constexpr float kGizmoTargetPixels = 90.0F;
constexpr float kGizmoHitThresholdPixels = 10.0F;
constexpr float kGizmoCenterHitRadiusPixels = 15.0F;
constexpr float kMinGizmoDepth = 0.25F;

[[nodiscard]] float DistanceToSegment2D(float px, float py, float ax, float ay, float bx, float by) noexcept {
    const float abX = bx - ax;
    const float abY = by - ay;
    const float apX = px - ax;
    const float apY = py - ay;
    const float abLengthSquared = abX * abX + abY * abY;
    if (abLengthSquared <= 0.0001F) {
        const float dx = px - ax;
        const float dy = py - ay;
        return std::sqrt(dx * dx + dy * dy);
    }

    const float t = std::clamp((apX * abX + apY * abY) / abLengthSquared, 0.0F, 1.0F);
    const float closestX = ax + abX * t;
    const float closestY = ay + abY * t;
    const float dx = px - closestX;
    const float dy = py - closestY;
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace

float EditorSceneViewportGizmoHitTester::ScreenSpaceScale(
    const EditorViewportCameraState& camera,
    const RECT& renderArea,
    kb::scene::Vec3 targetPosition) noexcept {
    const EditorViewportCameraAxes axes = camera.Axes();
    const float depth = std::max(kMinGizmoDepth, EditorSceneViewportMath::Dot(EditorSceneViewportMath::Sub(targetPosition, axes.position), axes.forward));
    const float tanHalfFov = std::tan(EditorSceneViewportMath::DegreesToRadians(camera.VerticalFovDegrees()) * 0.5F);
    const float worldPerPixel = (2.0F * depth * tanHalfFov) / EditorSceneViewportMath::RectHeight(renderArea);
    return std::clamp((kGizmoTargetPixels * worldPerPixel) / kGizmoAxisLength, 0.05F, 50000.0F);
}

int EditorSceneViewportGizmoHitTester::HitAxis(
    const EditorViewportCameraState& camera,
    const RECT& renderArea,
    kb::scene::Vec3 targetPosition,
    float worldScale,
    float localX,
    float localY) noexcept {
    float originX = 0.0F;
    float originY = 0.0F;
    if (!EditorSceneViewportMath::WorldToScreen(camera, renderArea, targetPosition, originX, originY)) {
        return -1;
    }

    int bestAxis = -1;
    float bestDistance = kGizmoHitThresholdPixels;
    for (int axis = 0; axis < 3; ++axis) {
        const kb::scene::Vec3 end = EditorSceneViewportMath::Add(
            targetPosition,
            EditorSceneViewportMath::Mul(EditorSceneViewportMath::AxisWorldDirection(axis), worldScale * kGizmoAxisLength));
        float endX = 0.0F;
        float endY = 0.0F;
        if (!EditorSceneViewportMath::WorldToScreen(camera, renderArea, end, endX, endY)) {
            continue;
        }

        const float distance = DistanceToSegment2D(localX, localY, originX, originY, endX, endY);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestAxis = axis;
        }
    }
    return bestAxis;
}

bool EditorSceneViewportGizmoHitTester::HitCenter(
    const EditorViewportCameraState& camera,
    const RECT& renderArea,
    kb::scene::Vec3 targetPosition,
    float localX,
    float localY) noexcept {
    float originX = 0.0F;
    float originY = 0.0F;
    if (!EditorSceneViewportMath::WorldToScreen(camera, renderArea, targetPosition, originX, originY)) {
        return false;
    }

    const float dx = localX - originX;
    const float dy = localY - originY;
    return std::sqrt(dx * dx + dy * dy) <= kGizmoCenterHitRadiusPixels;
}

} // namespace kb::editor

#endif
