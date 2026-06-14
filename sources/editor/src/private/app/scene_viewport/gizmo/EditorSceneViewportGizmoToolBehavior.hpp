#pragma once

#include "app/scene_viewport/EditorSceneViewportTypes.hpp"
#include "inspection/InspectorPanelState.hpp"
#include "scene/EditorSceneViewportStateStore.hpp"

namespace kb::editor {

#if defined(_WIN32)

class EditorSceneViewportGizmoToolBehavior {
public:
    EditorSceneViewportGizmoToolBehavior() = delete;

    [[nodiscard]] static const char* TransformEditLabel(EditorTransformToolMode mode) noexcept;
    [[nodiscard]] static InspectorPropertyId ScalePropertyForAxis(int axis) noexcept;
    [[nodiscard]] static int HitAxis(
        EditorTransformToolMode mode,
        const EditorViewportCameraState& camera,
        const RECT& renderArea,
        kb::scene::Vec3 targetPosition,
        float worldScale,
        float localX,
        float localY) noexcept;
};

#endif

} // namespace kb::editor
