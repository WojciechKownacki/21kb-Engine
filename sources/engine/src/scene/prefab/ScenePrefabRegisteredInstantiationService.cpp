#include "scene/prefab/ScenePrefabRegisteredInstantiationService.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabInstantiationService.hpp"
#include "scene/prefab/ScenePrefabNestedResolver.hpp"

#include <utility>
#include <vector>

namespace kb::scene {
namespace {

[[nodiscard]] std::vector<SceneObject> CopyObjects(const ScenePrefabInstance& instance) {
    return { instance.Objects().begin(), instance.Objects().end() };
}

} // namespace

ScenePrefabInstance ScenePrefabRegisteredInstantiationService::Instantiate(Scene& scene, ScenePrefabHandle handle, const ScenePrefabInstantiationSettings& settings) {
    std::vector<ScenePrefabInstance> instances = InstantiateMany(scene, handle, 1, settings);
    if (instances.empty()) {
        return {};
    }
    return std::move(instances.front());
}

std::vector<ScenePrefabInstance> ScenePrefabRegisteredInstantiationService::InstantiateMany(Scene& scene, ScenePrefabHandle handle, std::size_t count, const ScenePrefabInstantiationSettings& settings) {
    SceneState& state = SceneAccess::State(scene);
    const ScenePrefab* prefab = state.prefabs.Find(handle);
    if (prefab == nullptr || count == 0) {
        return {};
    }

    ScenePrefab resolvedPrefab = ScenePrefabNestedResolver::Resolve(state.prefabs, *prefab);
    std::vector<ScenePrefabInstance> instances = ScenePrefabInstantiationService::InstantiateMany(scene, resolvedPrefab, count, settings);
    for (ScenePrefabInstance& instance : instances) {
        std::vector<SceneObject> objects = CopyObjects(instance);
        ScenePrefabInstanceHandle instanceHandle = state.prefabInstances.Register(handle, settings.parent, objects, resolvedPrefab);
        instance = ScenePrefabInstance{ instanceHandle, std::move(objects) };
    }
    return instances;
}

} // namespace kb::scene
