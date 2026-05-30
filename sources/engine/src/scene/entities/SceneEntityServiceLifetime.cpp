#include "scene/SceneEntityService.hpp"

#include "scene/entities/SceneEntityDestructionService.hpp"

namespace kb::scene {

void SceneEntityService::DestroyObject(Scene& scene, SceneObject object) noexcept {
    SceneEntityDestructionService::DestroyObject(scene, object);
}

void SceneEntityService::DestroyEntity(Scene& scene, SceneEntity entity) noexcept {
    SceneEntityDestructionService::DestroyEntity(scene, entity);
}

} // namespace kb::scene
