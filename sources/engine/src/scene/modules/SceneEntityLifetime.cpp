#include "engine/scene/SceneEntities.hpp"

#include "scene/SceneEntityService.hpp"

namespace kb::scene {

void SceneEntities::Destroy(SceneObject object) noexcept {
    SceneEntityService::DestroyObject(scene_, object);
}

void SceneEntities::Destroy(SceneEntity entity) noexcept {
    SceneEntityService::DestroyEntity(scene_, entity);
}

} // namespace kb::scene
