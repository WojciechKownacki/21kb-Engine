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
        static_cast<void>(Write(scene, change.entity, change.before, change.beforeTranslation));
        result.touched.push_back(change.entity);
        result.changed = true;
    }
    return result;
}

bool EditorSceneTransformEditApplier::Write(kb::scene::Scene& scene, kb::scene::SceneEntity entity,
    const kb::scene::TransformComponent& transform, const kb::math::DVec3& translation) {
    const kb::scene::TransformComponent current = scene.Transforms().Get(entity);
    if (EditorSceneTransformEquality::Same(current, scene.Transforms().LocalTranslation(entity), transform, translation)) {
        return false;
    }
    scene.Transforms().Set(entity, transform);
    // A translation beyond what the float view holds keeps its residual; one the view holds drops an old residual.
    if (scene.Transforms().LocalTranslation(entity) != translation) {
        scene.Transforms().SetLocalTranslation(entity, translation);
    }
    return true;
}

} // namespace kb::editor
