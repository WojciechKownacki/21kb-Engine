#include "scene/prefab/ScenePrefabInstantiationService.hpp"

#include "engine/scene/ScenePrefabs.hpp"
#include "scene/prefab/ScenePrefabBulkInstantiationService.hpp"

#include <utility>

namespace kb::scene {

ScenePrefabInstance ScenePrefabInstantiationService::Instantiate(Scene& scene, const ScenePrefab& prefab, const ScenePrefabInstantiationSettings& settings) {
    std::vector<ScenePrefabInstance> instances = InstantiateMany(scene, prefab, 1, settings);
    if (instances.empty()) {
        return ScenePrefabInstance{};
    }
    return std::move(instances.front());
}

std::vector<ScenePrefabInstance> ScenePrefabInstantiationService::InstantiateMany(Scene& scene, const ScenePrefab& prefab, std::size_t count, const ScenePrefabInstantiationSettings& settings) {
    return ScenePrefabBulkInstantiationService::Instantiate(scene, prefab, count, settings);
}

ScenePrefabInstantiationStats ScenePrefabInstantiationService::InstantiateBatch(Scene& scene, const ScenePrefab& prefab, std::size_t count, const ScenePrefabInstantiationSettings& settings) {
    return ScenePrefabBulkInstantiationService::InstantiateBatch(scene, prefab, count, settings);
}

} // namespace kb::scene
