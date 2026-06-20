#include "engine/scene/SceneEntities.hpp"

#include "scene/SceneEntityService.hpp"

#include <utility>

namespace kb::scene {

SceneObject SceneEntities::CreateObject() {
    return SceneEntityService::CreateObject(scene_);
}

SceneObject SceneEntities::CreateObject(SceneObjectDesc desc) {
    return SceneEntityService::CreateObject(scene_, std::move(desc));
}

std::vector<SceneObject> SceneEntities::CreateObjects(std::span<const SceneObjectDesc> descs) {
    return SceneEntityService::CreateObjects(scene_, descs);
}

SceneEntity SceneEntities::CreateEntity() {
    return SceneEntityService::CreateEntity(scene_);
}

SceneEntity SceneEntities::CreateEntity(SceneObjectDesc desc) {
    return SceneEntityService::CreateEntity(scene_, std::move(desc));
}

SceneObject SceneEntities::Duplicate(SceneObject object) {
    return SceneEntityService::DuplicateObject(scene_, object);
}

std::vector<SceneObject> SceneEntities::Duplicate(std::span<const SceneObject> objects) {
    return SceneEntityService::DuplicateObjects(scene_, objects);
}

} // namespace kb::scene
