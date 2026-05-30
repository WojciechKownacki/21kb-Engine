#include "engine/scene/SceneEntities.hpp"

#include "scene/SceneEntityService.hpp"

namespace kb::scene {

std::string SceneEntities::Name(SceneObject object) const {
    return SceneEntityService::Name(scene_, object);
}

std::string SceneEntities::Name(SceneEntity entity) const {
    return SceneEntityService::Name(scene_, entity);
}

void SceneEntities::SetName(SceneObject object, std::string_view name) {
    SceneEntityService::SetName(scene_, object, name);
}

void SceneEntities::SetName(SceneEntity entity, std::string_view name) {
    SceneEntityService::SetName(scene_, entity, name);
}

} // namespace kb::scene
