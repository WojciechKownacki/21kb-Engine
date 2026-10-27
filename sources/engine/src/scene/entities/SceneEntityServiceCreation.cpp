#include "scene/SceneEntityService.hpp"

#include "scene/entities/SceneEntityCreationService.hpp"

#include <span>
#include <vector>

namespace kb::scene {

SceneObject SceneEntityService::CreateObject(Scene& scene) {
    return SceneEntityCreationService::CreateObject(scene);
}

SceneObject SceneEntityService::CreateObject(Scene& scene, const SceneObjectDesc& desc) {
    return SceneEntityCreationService::CreateObject(scene, desc);
}

std::vector<SceneObject> SceneEntityService::CreateObjects(Scene& scene, std::span<const SceneObjectDesc> descs) {
    return SceneEntityCreationService::CreateObjects(scene, descs);
}

SceneEntity SceneEntityService::CreateEntity(Scene& scene) {
    return SceneEntityCreationService::CreateEntity(scene);
}

SceneEntity SceneEntityService::CreateEntity(Scene& scene, const SceneObjectDesc& desc) {
    return SceneEntityCreationService::CreateEntity(scene, desc);
}

} // namespace kb::scene
