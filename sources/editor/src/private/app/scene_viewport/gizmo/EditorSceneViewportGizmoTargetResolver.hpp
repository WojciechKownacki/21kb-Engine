#pragma once

#include "engine/scene/TransformComponent.hpp"
#include "scene/EditorSceneContext.hpp"

#include <optional>

namespace kb::editor {

class EditorSceneViewportGizmoTargetResolver {
public:
    EditorSceneViewportGizmoTargetResolver() = delete;

    // The selection's pivot in viewport space: relative to `viewportOrigin`.
    [[nodiscard]] static std::optional<kb::scene::Vec3> SelectedTarget(EditorSceneContext& sceneContext, const kb::math::DVec3& viewportOrigin) noexcept;
};

} // namespace kb::editor
