#pragma once

#include "engine/scene/TransformComponent.hpp"
#include "scene/EditorSceneContext.hpp"

#include <optional>

namespace kb::editor {

class EditorSceneViewportGizmoTargetResolver {
public:
    EditorSceneViewportGizmoTargetResolver() = delete;

    [[nodiscard]] static std::optional<kb::scene::Vec3> SelectedTarget(EditorSceneContext& sceneContext) noexcept;
};

} // namespace kb::editor
