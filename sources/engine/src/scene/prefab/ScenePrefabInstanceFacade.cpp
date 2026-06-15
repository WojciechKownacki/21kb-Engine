#include "scene/prefab/ScenePrefabInstanceFacade.hpp"

#include "scene/prefab/ScenePrefabInstantiationService.hpp"
#include "scene/prefab/ScenePrefabRegisteredInstantiationService.hpp"

namespace kb::scene {

ScenePrefabInstance ScenePrefabInstanceFacade::Instantiate(Scene& scene, const ScenePrefab& prefab, const ScenePrefabInstantiationSettings& settings) {
    return ScenePrefabInstantiationService::Instantiate(scene, prefab, settings);
}

ScenePrefabInstance ScenePrefabInstanceFacade::Instantiate(Scene& scene, ScenePrefabHandle handle, const ScenePrefabInstantiationSettings& settings) {
    return ScenePrefabRegisteredInstantiationService::Instantiate(scene, handle, settings);
}

std::vector<ScenePrefabInstance> ScenePrefabInstanceFacade::InstantiateMany(Scene& scene, const ScenePrefab& prefab, std::size_t count, const ScenePrefabInstantiationSettings& settings) {
    return ScenePrefabInstantiationService::InstantiateMany(scene, prefab, count, settings);
}

std::vector<ScenePrefabInstance> ScenePrefabInstanceFacade::InstantiateMany(Scene& scene, ScenePrefabHandle handle, std::size_t count, const ScenePrefabInstantiationSettings& settings) {
    return ScenePrefabRegisteredInstantiationService::InstantiateMany(scene, handle, count, settings);
}

} // namespace kb::scene
