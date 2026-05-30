#include "scene/SceneEntityService.hpp"

#include "scene/entities/SceneEntityNameService.hpp"

namespace kb::scene {

std::string SceneEntityService::Name(const Scene& scene, SceneObject object) {
    return SceneEntityNameService::Name(scene, object);
}

std::string SceneEntityService::Name(const Scene& scene, SceneEntity entity) {
    return SceneEntityNameService::Name(scene, entity);
}

void SceneEntityService::SetName(Scene& scene, SceneObject object, std::string_view name) {
    SceneEntityNameService::SetName(scene, object, name);
}

void SceneEntityService::SetName(Scene& scene, SceneEntity entity, std::string_view name) {
    SceneEntityNameService::SetName(scene, entity, name);
}

} // namespace kb::scene
