#include "scene/SceneEntityService.hpp"

#include "scene/entities/SceneEntityBatchService.hpp"
#include "scene/entities/SceneEntityDuplicateService.hpp"

namespace kb::scene {

SceneObject SceneEntityService::DuplicateObject(Scene& scene, SceneObject object) {
    return SceneEntityDuplicateService::Duplicate(scene, object);
}

std::vector<SceneObject> SceneEntityService::DuplicateObjects(Scene& scene, std::span<const SceneObject> objects) {
    return SceneEntityDuplicateService::Duplicate(scene, objects);
}

void SceneEntityService::DestroyObjects(Scene& scene, std::span<const SceneObject> objects) noexcept {
    SceneEntityBatchService::Destroy(scene, objects);
}

bool SceneEntityService::SetParent(Scene& scene, std::span<const SceneObject> objects, SceneObject parent) noexcept {
    return SceneEntityBatchService::SetParent(scene, objects, parent);
}

} // namespace kb::scene
