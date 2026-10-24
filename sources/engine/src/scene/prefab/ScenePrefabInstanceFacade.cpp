#include "scene/prefab/ScenePrefabInstanceFacade.hpp"

#include "engine/scene/ScenePrefabs.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/prefab/ScenePrefabInstanceRelinker.hpp"
#include "scene/prefab/ScenePrefabInstantiationService.hpp"
#include "scene/prefab/ScenePrefabRegisteredInstantiationService.hpp"

#include <stdexcept>

namespace kb::scene {
namespace {

void ValidateParent(const Scene& scene, const ScenePrefabInstantiationSettings& settings, std::size_t count) {
    if (count != 0U && settings.parent.EntityHandle().IsValid() && !SceneEntityService::IsAlive(scene, settings.parent)) {
        throw std::invalid_argument("Prefab parent must be a live object in the target scene");
    }
}

} // namespace

ScenePrefabInstance ScenePrefabInstanceFacade::Instantiate(Scene& scene, const ScenePrefab& prefab, const ScenePrefabInstantiationSettings& settings) {
    ValidateParent(scene, settings, 1U);
    ScenePrefabInstance instance = ScenePrefabInstantiationService::Instantiate(scene, prefab, settings);
    if (settings.linkPrefabInstances) {
        ScenePrefabInstanceRelinker::Relink(scene, prefab, settings.parent, instance);
    }
    return instance;
}

ScenePrefabInstance ScenePrefabInstanceFacade::Instantiate(Scene& scene, ScenePrefabHandle handle, const ScenePrefabInstantiationSettings& settings) {
    ValidateParent(scene, settings, 1U);
    return ScenePrefabRegisteredInstantiationService::Instantiate(scene, handle, settings);
}

std::vector<ScenePrefabInstance> ScenePrefabInstanceFacade::InstantiateMany(Scene& scene, const ScenePrefab& prefab, std::size_t count, const ScenePrefabInstantiationSettings& settings) {
    ValidateParent(scene, settings, count);
    return ScenePrefabInstantiationService::InstantiateMany(scene, prefab, count, settings);
}

std::vector<ScenePrefabInstance> ScenePrefabInstanceFacade::InstantiateMany(Scene& scene, ScenePrefabHandle handle, std::size_t count, const ScenePrefabInstantiationSettings& settings) {
    ValidateParent(scene, settings, count);
    return ScenePrefabRegisteredInstantiationService::InstantiateMany(scene, handle, count, settings);
}

ScenePrefabInstantiationStats ScenePrefabInstanceFacade::InstantiateBatch(Scene& scene, const ScenePrefab& prefab, std::size_t count, const ScenePrefabInstantiationSettings& settings) {
    ValidateParent(scene, settings, count);
    return ScenePrefabInstantiationService::InstantiateBatch(scene, prefab, count, settings);
}

ScenePrefabInstantiationStats ScenePrefabInstanceFacade::InstantiateBatch(Scene& scene, ScenePrefabHandle handle, std::size_t count, const ScenePrefabInstantiationSettings& settings) {
    ValidateParent(scene, settings, count);
    return ScenePrefabRegisteredInstantiationService::InstantiateBatch(scene, handle, count, settings);
}

} // namespace kb::scene
