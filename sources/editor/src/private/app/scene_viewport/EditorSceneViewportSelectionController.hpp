#pragma once

#include "app/scene_viewport/EditorSceneViewportSelectionTypes.hpp"
#include "scene/EditorSceneContext.hpp"

#include <span>

namespace kb::editor {

#if defined(_WIN32)

class EditorSceneViewportSelectionController {
public:
    EditorSceneViewportSelectionController() = delete;

    static void ApplyClick(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, EditorSceneViewportSelectionMode mode);
    static void ApplyBox(EditorSceneContext& sceneContext, std::span<const kb::scene::SceneEntity> entities, EditorSceneViewportSelectionMode mode);
};

#endif

} // namespace kb::editor
