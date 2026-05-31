#include "scene/prefab/ScenePrefabRegistryFacade.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

#include <utility>

namespace kb::scene {

ScenePrefabHandle ScenePrefabRegistryFacade::Register(Scene& scene, std::string name, ScenePrefab prefab) {
    return SceneAccess::State(scene).prefabs.Register(std::move(name), std::move(prefab));
}

ScenePrefabHandle ScenePrefabRegistryFacade::RegisterVariant(Scene& scene, std::string name, ScenePrefabHandle basePrefab, std::vector<ScenePrefabPropertyOverride> overrides) {
    return SceneAccess::State(scene).prefabs.RegisterVariant(std::move(name), basePrefab, std::move(overrides));
}

bool ScenePrefabRegistryFacade::Contains(Scene& scene, ScenePrefabHandle handle) noexcept {
    return SceneAccess::State(scene).prefabs.Contains(handle);
}

std::string ScenePrefabRegistryFacade::Guid(Scene& scene, ScenePrefabHandle handle) {
    const ScenePrefabRecord* record = SceneAccess::State(scene).prefabs.FindRecord(handle);
    return record == nullptr ? std::string{} : record->guid;
}

std::size_t ScenePrefabRegistryFacade::Count(Scene& scene) noexcept {
    return SceneAccess::State(scene).prefabs.Count();
}

ScenePrefab ScenePrefabRegistryFacade::Get(Scene& scene, ScenePrefabHandle handle) {
    const ScenePrefab* prefab = SceneAccess::State(scene).prefabs.Find(handle);
    return prefab == nullptr ? ScenePrefab{} : *prefab;
}

} // namespace kb::scene
