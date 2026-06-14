#pragma once

#include "app/scene_viewport/EditorSceneViewportTypes.hpp"
#include "scene/EditorSceneContext.hpp"

namespace kb::editor {

#if defined(_WIN32)

class EditorSceneViewportGizmoDragUpdater {
public:
    EditorSceneViewportGizmoDragUpdater() = delete;

    [[nodiscard]] static bool Update(
        EditorSceneContext& sceneContext,
        const EditorSceneViewportHit& hit,
        kb::scene::Vec3 dragStartTarget);

private:
    [[nodiscard]] static bool UpdateCenterDrag(
        EditorSceneContext& sceneContext,
        const EditorSceneViewportHit& hit,
        kb::scene::Vec3 dragStartTarget);
    [[nodiscard]] static bool UpdateAxisDrag(
        EditorSceneContext& sceneContext,
        const EditorSceneViewportHit& hit,
        kb::scene::Vec3 dragStartTarget);
};

#endif

} // namespace kb::editor
