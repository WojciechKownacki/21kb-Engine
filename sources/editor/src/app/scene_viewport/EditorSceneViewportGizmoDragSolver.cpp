#include "app/scene_viewport/EditorSceneViewportGizmoDragSolver.hpp"

#if defined(_WIN32)
#include <cmath>

namespace kb::editor {
namespace {

constexpr float kGizmoParallelEps = 1.0e-5F;
constexpr float kGizmoMinRayDistance = 1.0e-3F;

void AxisDragPlane(kb::scene::Vec3 cameraForward, int axis, kb::scene::Vec3& planeNormal, kb::scene::Vec3& removeNormal) noexcept {
    const float cx = std::abs(cameraForward.x);
    const float cy = std::abs(cameraForward.y);
    const float cz = std::abs(cameraForward.z);

    if (axis == 0) {
        planeNormal = cy > cz ? EditorSceneViewportMath::AxisWorldDirection(1) : EditorSceneViewportMath::AxisWorldDirection(2);
        removeNormal = cy > cz ? EditorSceneViewportMath::AxisWorldDirection(2) : EditorSceneViewportMath::AxisWorldDirection(1);
        return;
    }
    if (axis == 1) {
        planeNormal = cx > cz ? EditorSceneViewportMath::AxisWorldDirection(0) : EditorSceneViewportMath::AxisWorldDirection(2);
        removeNormal = cx > cz ? EditorSceneViewportMath::AxisWorldDirection(2) : EditorSceneViewportMath::AxisWorldDirection(0);
        return;
    }

    planeNormal = cx > cy ? EditorSceneViewportMath::AxisWorldDirection(0) : EditorSceneViewportMath::AxisWorldDirection(1);
    removeNormal = cx > cy ? EditorSceneViewportMath::AxisWorldDirection(1) : EditorSceneViewportMath::AxisWorldDirection(0);
}

void StoreVec3(float (&target)[3], kb::scene::Vec3 value) noexcept {
    target[0] = value.x;
    target[1] = value.y;
    target[2] = value.z;
}

[[nodiscard]] kb::scene::Vec3 LoadVec3(const float (&value)[3]) noexcept {
    return kb::scene::Vec3{value[0], value[1], value[2]};
}

} // namespace

bool EditorSceneViewportGizmoDragSolver::PlaneDragPosition(
    const EditorSceneViewportRay& ray,
    kb::scene::Vec3 planePoint,
    kb::scene::Vec3 planeNormal,
    kb::scene::Vec3& hit) noexcept {
    planeNormal = EditorSceneViewportMath::Normalize(planeNormal);
    const float denom = EditorSceneViewportMath::Dot(ray.direction, planeNormal);
    if (std::abs(denom) <= kGizmoParallelEps) {
        return false;
    }

    const float distance = EditorSceneViewportMath::Dot(EditorSceneViewportMath::Sub(planePoint, ray.origin), planeNormal) / denom;
    if (distance <= kGizmoMinRayDistance) {
        return false;
    }

    hit = EditorSceneViewportMath::Add(ray.origin, EditorSceneViewportMath::Mul(ray.direction, distance));
    return true;
}

bool EditorSceneViewportGizmoDragSolver::BeginAxisDrag(
    const EditorSceneViewportHit& hit,
    const EditorViewportCameraState& camera,
    kb::scene::Vec3 targetPosition,
    int axis,
    EditorSceneGizmoAxisDrag& drag) noexcept {
    kb::scene::Vec3 planeNormal{};
    kb::scene::Vec3 removeNormal{};
    AxisDragPlane(camera.Axes().forward, axis, planeNormal, removeNormal);

    kb::scene::Vec3 startPoint{};
    if (!PlaneDragPosition(hit.ray, targetPosition, planeNormal, startPoint)) {
        return false;
    }

    StoreVec3(drag.axis, EditorSceneViewportMath::AxisWorldDirection(axis));
    StoreVec3(drag.planeNormal, planeNormal);
    StoreVec3(drag.removeNormal, removeNormal);
    StoreVec3(drag.startPoint, startPoint);
    return true;
}

bool EditorSceneViewportGizmoDragSolver::AxisDragDelta(
    const EditorSceneViewportHit& hit,
    kb::scene::Vec3 targetPosition,
    const EditorSceneGizmoAxisDrag& drag,
    kb::scene::Vec3& delta) noexcept {
    kb::scene::Vec3 currentPoint{};
    if (!PlaneDragPosition(hit.ray, targetPosition, LoadVec3(drag.planeNormal), currentPoint)) {
        return false;
    }

    const kb::scene::Vec3 rawDelta = EditorSceneViewportMath::Sub(currentPoint, LoadVec3(drag.startPoint));
    const kb::scene::Vec3 removeNormal = LoadVec3(drag.removeNormal);
    const kb::scene::Vec3 planarDelta = EditorSceneViewportMath::Sub(rawDelta, EditorSceneViewportMath::Mul(removeNormal, EditorSceneViewportMath::Dot(rawDelta, removeNormal)));
    const kb::scene::Vec3 axis = LoadVec3(drag.axis);
    delta = EditorSceneViewportMath::Mul(axis, EditorSceneViewportMath::Dot(planarDelta, axis));
    return true;
}

} // namespace kb::editor

#endif
