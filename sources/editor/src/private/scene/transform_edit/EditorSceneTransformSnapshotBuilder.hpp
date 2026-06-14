#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "scene/transform_edit/EditorSceneTransformChange.hpp"

#include <span>
#include <vector>

namespace kb::scene {

class Scene;

} // namespace kb::scene

namespace kb::editor {

class EditorSceneTransformSnapshotBuilder {
public:
    EditorSceneTransformSnapshotBuilder() = delete;

    [[nodiscard]] static std::vector<EditorSceneObjectTransformChange> Capture(
        kb::scene::Scene& scene,
        std::span<const kb::scene::SceneEntity> entities);
};

} // namespace kb::editor
