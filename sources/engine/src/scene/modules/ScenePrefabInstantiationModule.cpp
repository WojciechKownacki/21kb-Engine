#include "engine/scene/ScenePrefabs.hpp"

#include "scene/prefab/ScenePrefabInstanceFacade.hpp"

namespace kb::scene {

ScenePrefabInstance ScenePrefabs::Instantiate(const ScenePrefab& prefab) {
    return Instantiate(prefab, ScenePrefabInstantiationSettings{});
}

ScenePrefabInstance ScenePrefabs::Instantiate(const ScenePrefab& prefab, const ScenePrefabInstantiationSettings& settings) {
    return ScenePrefabInstanceFacade::Instantiate(scene_, prefab, settings);
}

std::vector<ScenePrefabInstance> ScenePrefabs::InstantiateMany(const ScenePrefab& prefab, std::size_t count) {
    return InstantiateMany(prefab, count, ScenePrefabInstantiationSettings{});
}

std::vector<ScenePrefabInstance> ScenePrefabs::InstantiateMany(const ScenePrefab& prefab, std::size_t count, const ScenePrefabInstantiationSettings& settings) {
    return ScenePrefabInstanceFacade::InstantiateMany(scene_, prefab, count, settings);
}

ScenePrefabInstance ScenePrefabs::Instantiate(ScenePrefabHandle handle) {
    return Instantiate(handle, ScenePrefabInstantiationSettings{});
}

ScenePrefabInstance ScenePrefabs::Instantiate(ScenePrefabHandle handle, const ScenePrefabInstantiationSettings& settings) {
    return ScenePrefabInstanceFacade::Instantiate(scene_, handle, settings);
}

std::vector<ScenePrefabInstance> ScenePrefabs::InstantiateMany(ScenePrefabHandle handle, std::size_t count) {
    return InstantiateMany(handle, count, ScenePrefabInstantiationSettings{});
}

std::vector<ScenePrefabInstance> ScenePrefabs::InstantiateMany(ScenePrefabHandle handle, std::size_t count, const ScenePrefabInstantiationSettings& settings) {
    return ScenePrefabInstanceFacade::InstantiateMany(scene_, handle, count, settings);
}

} // namespace kb::scene
