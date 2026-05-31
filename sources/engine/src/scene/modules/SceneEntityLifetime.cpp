#include "engine/scene/SceneEntities.hpp"

#include "scene/SceneEntityService.hpp"

namespace kb::scene {

void SceneEntities::Destroy(SceneObject object) noexcept {
    SceneEntityService::DestroyObject(scene_, object);
}

void SceneEntities::Destroy(SceneEntity entity) noexcept {
    SceneEntityService::DestroyEntity(scene_, entity);
}

void SceneEntities::Destroy(std::span<const SceneObject> objects) noexcept {
    SceneEntityService::DestroyObjects(scene_, objects);
}

bool SceneEntities::SetParent(std::span<const SceneObject> objects, SceneObject parent) noexcept {
    return SceneEntityService::SetParent(scene_, objects, parent);
}

} // namespace kb::scene
