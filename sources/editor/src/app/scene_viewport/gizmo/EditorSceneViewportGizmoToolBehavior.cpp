#include "app/scene_viewport/gizmo/EditorSceneViewportGizmoToolBehavior.hpp"

#if defined(_WIN32)
#include "app/scene_viewport/EditorSceneViewportGizmoHitTester.hpp"

namespace kb::editor {

const char* EditorSceneViewportGizmoToolBehavior::TransformEditLabel(EditorTransformToolMode mode) noexcept {
    switch (mode) {
    case EditorTransformToolMode::Translate:
        return "Move Entity";
    case EditorTransformToolMode::Rotate:
        return "Rotate Entity";
    case EditorTransformToolMode::Scale:
        return "Scale Entity";
    }
    return "Edit Transform";
}

InspectorPropertyId EditorSceneViewportGizmoToolBehavior::ScalePropertyForAxis(int axis) noexcept {
    switch (axis) {
    case 0:
        return InspectorPropertyId::ScaleX;
    case 1:
        return InspectorPropertyId::ScaleY;
    case 2:
        return InspectorPropertyId::ScaleZ;
    default:
        return InspectorPropertyId::None;
    }
}

int EditorSceneViewportGizmoToolBehavior::HitAxis(
    EditorTransformToolMode mode,
    const EditorViewportCameraState& camera,
    const RECT& renderArea,
    kb::scene::Vec3 targetPosition,
    float worldScale,
    float localX,
    float localY) noexcept {
    if (mode == EditorTransformToolMode::Rotate) {
        return EditorSceneViewportGizmoHitTester::HitRotationAxis(camera, renderArea, targetPosition, worldScale, localX, localY);
    }
    return EditorSceneViewportGizmoHitTester::HitAxis(camera, renderArea, targetPosition, worldScale, localX, localY);
}

} // namespace kb::editor

#endif
