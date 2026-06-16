#include "scene/prefab/ScenePrefabRegisteredInstantiationService.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabInstantiationService.hpp"
#include "scene/prefab/ScenePrefabNestedResolver.hpp"
#include "scene/prefab/ScenePrefabRecord.hpp"

#include <utility>
#include <span>
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
    const ScenePrefabRecord* record = state.prefabs.FindRecord(handle);
    if (record == nullptr || count == 0) {
        return {};
    }

    ScenePrefab resolvedPrefab = ScenePrefabNestedResolver::Resolve(state.prefabs, record->prefab);
    std::vector<ScenePrefabInstance> instances = ScenePrefabInstantiationService::InstantiateMany(scene, resolvedPrefab, count, settings);
    std::vector<std::vector<SceneObject>> objectSets;
    objectSets.reserve(instances.size());
    for (const ScenePrefabInstance& instance : instances) {
        objectSets.push_back(CopyObjects(instance));
    }

    const std::vector<ScenePrefabInstanceHandle> handles = state.prefabInstances.RegisterMany(
        handle,
        record->guid,
        settings.parent,
        std::span<const std::vector<SceneObject>>{ objectSets },
        resolvedPrefab);
    if (handles.size() != instances.size()) {
        return {};
    }

    for (std::size_t index = 0; index < instances.size(); ++index) {
        instances[index] = ScenePrefabInstance{ handles[index], std::move(objectSets[index]) };
    }
    return instances;
}

} // namespace kb::scene
