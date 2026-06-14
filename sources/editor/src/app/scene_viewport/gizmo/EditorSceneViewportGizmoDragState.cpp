#include "app/scene_viewport/gizmo/EditorSceneViewportGizmoDragState.hpp"

namespace kb::editor {

kb::scene::Vec3 EditorSceneViewportGizmoDragState::DragStartTarget(const EditorSceneGizmoState& gizmo) noexcept {
    return kb::scene::Vec3{ gizmo.dragStartTargetX, gizmo.dragStartTargetY, gizmo.dragStartTargetZ };
}

void EditorSceneViewportGizmoDragState::StartCenterDrag(
    EditorSceneGizmoState& gizmo,
    kb::scene::Vec3 targetPosition,
    kb::scene::Vec3 planeNormal,
    kb::scene::Vec3 startPoint) noexcept {
    gizmo.draggedAxis = -1;
    gizmo.hoveredAxis = -1;
    gizmo.centerDrag = true;
    gizmo.dragStartTargetX = targetPosition.x;
    gizmo.dragStartTargetY = targetPosition.y;
    gizmo.dragStartTargetZ = targetPosition.z;
    gizmo.centerPlaneNx = planeNormal.x;
    gizmo.centerPlaneNy = planeNormal.y;
    gizmo.centerPlaneNz = planeNormal.z;
    gizmo.centerStartPx = startPoint.x;
    gizmo.centerStartPy = startPoint.y;
    gizmo.centerStartPz = startPoint.z;
}

void EditorSceneViewportGizmoDragState::StartAxisDrag(
    EditorSceneGizmoState& gizmo,
    kb::scene::Vec3 targetPosition,
    int axis,
    const EditorSceneGizmoAxisDrag& drag,
    float screenAngle) noexcept {
    gizmo.hoveredAxis = axis;
    gizmo.draggedAxis = axis;
    gizmo.centerDrag = false;
    gizmo.dragStartTargetX = targetPosition.x;
    gizmo.dragStartTargetY = targetPosition.y;
    gizmo.dragStartTargetZ = targetPosition.z;
    gizmo.dragStartScreenAngle = screenAngle;
    gizmo.axisDrag = drag;
}

void EditorSceneViewportGizmoDragState::ClearActiveDrag(EditorSceneGizmoState& gizmo) noexcept {
    gizmo.draggedAxis = -1;
    gizmo.centerDrag = false;
    gizmo.ClearDragPointer();
}

} // namespace kb::editor
