#include "scene/entities/SceneEntityBatchService.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"

namespace kb::scene {

void SceneEntityBatchService::Destroy(Scene& scene, std::span<const SceneObject> objects) noexcept {
    for (const SceneObject object : objects) {
        scene.Entities().Destroy(object);
    }
}

bool SceneEntityBatchService::SetParent(Scene& scene, std::span<const SceneObject> objects, SceneObject parent) noexcept {
    for (const SceneObject object : objects) {
        if (!object.IsValid() || !scene.Entities().IsAlive(object) || (parent.IsValid() && !scene.Entities().IsAlive(parent))) {
            return false;
        }
    }

    bool changed = false;
    for (const SceneObject object : objects) {
        changed = scene.Hierarchy().SetParent(object, parent) || changed;
    }
    return changed || objects.empty();
}

} // namespace kb::scene
