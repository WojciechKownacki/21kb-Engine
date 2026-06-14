#pragma once

#include "engine/scene/TransformComponent.hpp"
#include "scene/EditorSceneViewportStateStore.hpp"

namespace kb::editor {

class EditorSceneViewportGizmoDragState {
public:
    EditorSceneViewportGizmoDragState() = delete;

    [[nodiscard]] static kb::scene::Vec3 DragStartTarget(const EditorSceneGizmoState& gizmo) noexcept;
    static void StartCenterDrag(
        EditorSceneGizmoState& gizmo,
        kb::scene::Vec3 targetPosition,
        kb::scene::Vec3 planeNormal,
        kb::scene::Vec3 startPoint) noexcept;
    static void StartAxisDrag(
        EditorSceneGizmoState& gizmo,
        kb::scene::Vec3 targetPosition,
        int axis,
        const EditorSceneGizmoAxisDrag& drag,
        float screenAngle) noexcept;
    static void ClearActiveDrag(EditorSceneGizmoState& gizmo) noexcept;
};

} // namespace kb::editor
