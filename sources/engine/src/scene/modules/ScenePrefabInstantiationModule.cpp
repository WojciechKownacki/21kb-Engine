#include "engine/scene/ScenePrefabs.hpp"

#include "scene/prefab/ScenePrefabInstanceFacade.hpp"

namespace kb::scene {

ScenePrefabInstance ScenePrefabs::Instantiate(const ScenePrefab& prefab) {
    return Instantiate(prefab, ScenePrefabInstantiationSettings{});
}

ScenePrefabInstance ScenePrefabs::Instantiate(const ScenePrefab& prefab, const ScenePrefabInstantiationSettings& settings) {
    return ScenePrefabInstanceFacade::Instantiate(scene_, prefab, settings);
}

ScenePrefabInstance ScenePrefabs::Instantiate(ScenePrefabHandle handle) {
    return Instantiate(handle, ScenePrefabInstantiationSettings{});
}

ScenePrefabInstance ScenePrefabs::Instantiate(ScenePrefabHandle handle, const ScenePrefabInstantiationSettings& settings) {
    return ScenePrefabInstanceFacade::Instantiate(scene_, handle, settings);
}

} // namespace kb::scene
