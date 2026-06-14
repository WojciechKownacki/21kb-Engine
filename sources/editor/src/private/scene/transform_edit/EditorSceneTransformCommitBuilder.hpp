#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "scene/transform_edit/EditorSceneTransformChange.hpp"
#include "scene/transform_edit/EditorSceneTransformEditSession.hpp"

#include <vector>

namespace kb::scene {

class Scene;

} // namespace kb::scene

namespace kb::editor {

class EditorSceneTransformCommitBuilder {
public:
    EditorSceneTransformCommitBuilder() = delete;

    [[nodiscard]] static std::vector<EditorSceneObjectTransformChange> Build(
        kb::scene::Scene& scene,
        EditorSceneTransformEditSession& session);
    [[nodiscard]] static std::vector<kb::scene::SceneEntity> TouchedEntities(
        const std::vector<EditorSceneObjectTransformChange>& changes);
};

} // namespace kb::editor
