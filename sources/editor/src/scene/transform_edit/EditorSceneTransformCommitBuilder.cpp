#include "scene/transform_edit/EditorSceneTransformCommitBuilder.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "scene/transform_edit/EditorSceneTransformEquality.hpp"

namespace kb::editor {

std::vector<EditorSceneObjectTransformChange> EditorSceneTransformCommitBuilder::Build(
    kb::scene::Scene& scene,
    EditorSceneTransformEditSession& session) {
    std::vector<EditorSceneObjectTransformChange> committed;
    committed.reserve(session.Changes().size());
    for (EditorSceneObjectTransformChange& change : session.Changes()) {
        if (!scene.Entities().IsAlive(change.entity)) {
            continue;
        }
        if (const kb::scene::TransformComponent* current = scene.Transforms().TryGet(change.entity); current != nullptr) {
            change.after = *current;
        }
        if (!EditorSceneTransformEquality::Same(change.before, change.after)) {
            committed.push_back(change);
        }
    }
    return committed;
}

std::vector<kb::scene::SceneEntity> EditorSceneTransformCommitBuilder::TouchedEntities(
    const std::vector<EditorSceneObjectTransformChange>& changes) {
    std::vector<kb::scene::SceneEntity> touched;
    touched.reserve(changes.size());
    for (const EditorSceneObjectTransformChange& change : changes) {
        touched.push_back(change.entity);
    }
    return touched;
}

} // namespace kb::editor
