#pragma once

#include "app/scene_viewport/EditorSceneViewportSelectionTypes.hpp"
#include "app/scene_viewport/EditorSceneViewportTypes.hpp"
#include "engine/scene/Scene.hpp"

#include <vector>

namespace kb::editor {

#if defined(_WIN32)

class EditorSceneViewportMeshPicker {
public:
    EditorSceneViewportMeshPicker() = delete;

    [[nodiscard]] static EditorSceneViewportPickResult PickNearest(const kb::scene::Scene& scene, const EditorSceneViewportRay& ray);
    [[nodiscard]] static std::vector<kb::scene::SceneEntity> PickInsideRect(
        const kb::scene::Scene& scene,
        const EditorViewportCameraState& camera,
        const RECT& renderArea,
        const RECT& selectionRect);
};

#endif

} // namespace kb::editor
