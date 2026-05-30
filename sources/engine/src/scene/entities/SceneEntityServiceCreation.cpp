#include "scene/SceneEntityService.hpp"

#include "scene/entities/SceneEntityCreationService.hpp"

#include <utility>

namespace kb::scene {

SceneObject SceneEntityService::CreateObject(Scene& scene) {
    return SceneEntityCreationService::CreateObject(scene);
}

SceneObject SceneEntityService::CreateObject(Scene& scene, SceneObjectDesc desc) {
    return SceneEntityCreationService::CreateObject(scene, std::move(desc));
}

SceneEntity SceneEntityService::CreateEntity(Scene& scene) {
    return SceneEntityCreationService::CreateEntity(scene);
}

SceneEntity SceneEntityService::CreateEntity(Scene& scene, SceneObjectDesc desc) {
    return SceneEntityCreationService::CreateEntity(scene, std::move(desc));
}

} // namespace kb::scene
