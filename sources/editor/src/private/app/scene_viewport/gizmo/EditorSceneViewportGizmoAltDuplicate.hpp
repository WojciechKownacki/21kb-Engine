#pragma once

#include "engine/scene/TransformComponent.hpp"
#include "scene/EditorSceneContext.hpp"

#include <optional>

namespace kb::editor {

class EditorSceneViewportGizmoAltDuplicate {
public:
    EditorSceneViewportGizmoAltDuplicate() = delete;

    [[nodiscard]] static bool DuplicateForTranslateDrag(EditorSceneContext& sceneContext, std::optional<kb::scene::Vec3>& targetPosition);
};

} // namespace kb::editor
