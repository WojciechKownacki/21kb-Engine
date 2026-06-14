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
constexpr float kGizmoRotationRingRadius = 0.792F;
constexpr float kGizmoRotationHitThresholdPixels = 12.0F;
constexpr float kMinGizmoDepth = 0.25F;
constexpr float kPi = 3.14159265358979323846F;

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

[[nodiscard]] kb::scene::Vec3 RingBasisU(int axis) noexcept {
    switch (axis) {
    case 0: return kb::scene::Vec3{0.0F, 1.0F, 0.0F};
    case 1: return kb::scene::Vec3{1.0F, 0.0F, 0.0F};
    case 2:
    default:
        return kb::scene::Vec3{1.0F, 0.0F, 0.0F};
    }
}

[[nodiscard]] kb::scene::Vec3 RingBasisV(int axis) noexcept {
    switch (axis) {
    case 0: return kb::scene::Vec3{0.0F, 0.0F, 1.0F};
    case 1: return kb::scene::Vec3{0.0F, 0.0F, 1.0F};
    case 2:
    default:
        return kb::scene::Vec3{0.0F, 1.0F, 0.0F};
    }
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

int EditorSceneViewportGizmoHitTester::HitRotationAxis(
    const EditorViewportCameraState& camera,
    const RECT& renderArea,
    kb::scene::Vec3 targetPosition,
    float worldScale,
    float localX,
    float localY) noexcept {
    constexpr int kSegments = 72;
    int bestAxis = -1;
    float bestDistance = kGizmoRotationHitThresholdPixels;
    const float radius = worldScale * kGizmoRotationRingRadius;

    for (int axis = 0; axis < 3; ++axis) {
        const kb::scene::Vec3 u = RingBasisU(axis);
        const kb::scene::Vec3 v = RingBasisV(axis);
        float prevX = 0.0F;
        float prevY = 0.0F;
        bool hasPrev = false;
        for (int segment = 0; segment <= kSegments; ++segment) {
            const float angle = (static_cast<float>(segment) / static_cast<float>(kSegments)) * 2.0F * kPi;
            const kb::scene::Vec3 point = EditorSceneViewportMath::Add(
                targetPosition,
                EditorSceneViewportMath::Mul(
                    EditorSceneViewportMath::Add(
                        EditorSceneViewportMath::Mul(u, std::cos(angle)),
                        EditorSceneViewportMath::Mul(v, std::sin(angle))),
                    radius));
            float screenX = 0.0F;
            float screenY = 0.0F;
            const bool visible = EditorSceneViewportMath::WorldToScreen(camera, renderArea, point, screenX, screenY);
            if (visible && hasPrev) {
                const float distance = DistanceToSegment2D(localX, localY, prevX, prevY, screenX, screenY);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    bestAxis = axis;
                }
            }
            hasPrev = visible;
            prevX = screenX;
            prevY = screenY;
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
