#pragma once

#include "app/scene_viewport/EditorSceneViewportTypes.hpp"
#include "scene/EditorSceneContext.hpp"

namespace kb::editor {

#if defined(_WIN32)

class EditorSceneViewportGizmoRotationDrag {
public:
    EditorSceneViewportGizmoRotationDrag() = delete;

    [[nodiscard]] static float ScreenAngleFromCenter(
        const EditorViewportCameraState& camera,
        const RECT& renderArea,
        kb::scene::Vec3 targetPosition,
        float localX,
        float localY) noexcept;
    [[nodiscard]] static bool Apply(
        EditorSceneContext& sceneContext,
        const EditorSceneViewportHit& hit,
        kb::scene::Vec3 dragStartTarget);
};

#endif

} // namespace kb::editor
