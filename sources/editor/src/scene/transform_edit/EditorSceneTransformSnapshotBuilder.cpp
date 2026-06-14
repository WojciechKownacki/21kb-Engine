#include "scene/transform_edit/EditorSceneTransformSnapshotBuilder.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <algorithm>

namespace kb::editor {
namespace {

[[nodiscard]] bool ContainsChange(
    std::span<const EditorSceneObjectTransformChange> changes,
    kb::scene::SceneEntity entity) noexcept {
    return std::ranges::find_if(changes, [entity](const EditorSceneObjectTransformChange& change) {
        return change.entity == entity;
    }) != changes.end();
}

} // namespace

std::vector<EditorSceneObjectTransformChange> EditorSceneTransformSnapshotBuilder::Capture(
    kb::scene::Scene& scene,
    std::span<const kb::scene::SceneEntity> entities) {
    std::vector<EditorSceneObjectTransformChange> changes;
    changes.reserve(entities.size());
    for (const kb::scene::SceneEntity entity : entities) {
        if (!scene.Entities().IsAlive(entity) || ContainsChange(changes, entity)) {
            continue;
        }
        const kb::scene::TransformComponent* transform = scene.Transforms().TryGet(entity);
        if (transform == nullptr) {
            continue;
        }
        changes.push_back(EditorSceneObjectTransformChange{
            .entity = entity,
            .before = *transform,
            .after = *transform,
        });
    }
    return changes;
}

} // namespace kb::editor
