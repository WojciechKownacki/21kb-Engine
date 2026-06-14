#include "scene/transform_edit/EditorSceneTransformEditApplier.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTransforms.hpp"

namespace kb::editor {

EditorSceneTransformEditApplyResult EditorSceneTransformEditApplier::RestoreBefore(
    kb::scene::Scene& scene,
    std::span<const EditorSceneObjectTransformChange> changes) {
    EditorSceneTransformEditApplyResult result{};
    result.touched.reserve(changes.size());
    for (const EditorSceneObjectTransformChange& change : changes) {
        if (!scene.Entities().IsAlive(change.entity)) {
            continue;
        }
        scene.Transforms().Set(change.entity, change.before);
        result.touched.push_back(change.entity);
        result.changed = true;
    }
    return result;
}

} // namespace kb::editor
