#include "scene/SceneEntityService.hpp"

#include "scene/entities/SceneEntityCreationService.hpp"

#include <span>
#include <utility>
#include <vector>

namespace kb::scene {

SceneObject SceneEntityService::CreateObject(Scene& scene) {
    return SceneEntityCreationService::CreateObject(scene);
}

SceneObject SceneEntityService::CreateObject(Scene& scene, SceneObjectDesc desc) {
    return SceneEntityCreationService::CreateObject(scene, std::move(desc));
}

std::vector<SceneObject> SceneEntityService::CreateObjects(Scene& scene, std::span<const SceneObjectDesc> descs) {
    return SceneEntityCreationService::CreateObjects(scene, descs);
}

SceneEntity SceneEntityService::CreateEntity(Scene& scene) {
    return SceneEntityCreationService::CreateEntity(scene);
}

SceneEntity SceneEntityService::CreateEntity(Scene& scene, SceneObjectDesc desc) {
    return SceneEntityCreationService::CreateEntity(scene, std::move(desc));
}

} // namespace kb::scene
