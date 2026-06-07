#pragma once

#include "app/scene_viewport/EditorSceneViewportTypes.hpp"
#include "scene/EditorSceneContext.hpp"

namespace kb::editor {

#if defined(_WIN32)

class EditorSceneViewportGizmoDragSolver {
public:
    EditorSceneViewportGizmoDragSolver() = delete;

    [[nodiscard]] static bool PlaneDragPosition(
        const EditorSceneViewportRay& ray,
        kb::scene::Vec3 planePoint,
        kb::scene::Vec3 planeNormal,
        kb::scene::Vec3& hit) noexcept;

    [[nodiscard]] static bool BeginAxisDrag(
        const EditorSceneViewportHit& hit,
        const EditorViewportCameraState& camera,
        kb::scene::Vec3 targetPosition,
        int axis,
        EditorSceneGizmoAxisDrag& drag) noexcept;

    [[nodiscard]] static bool AxisDragDelta(
        const EditorSceneViewportHit& hit,
        kb::scene::Vec3 targetPosition,
        const EditorSceneGizmoAxisDrag& drag,
        kb::scene::Vec3& delta) noexcept;
};

#endif

} // namespace kb::editor
