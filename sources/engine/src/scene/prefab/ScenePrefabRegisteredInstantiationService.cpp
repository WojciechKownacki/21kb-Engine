#include "scene/prefab/ScenePrefabRegisteredInstantiationService.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabInstantiationService.hpp"

#include <utility>
#include <vector>

namespace kb::scene {
namespace {

[[nodiscard]] std::vector<SceneObject> CopyObjects(const ScenePrefabInstance& instance) {
    return { instance.Objects().begin(), instance.Objects().end() };
}

} // namespace

ScenePrefabInstance ScenePrefabRegisteredInstantiationService::Instantiate(Scene& scene, ScenePrefabHandle handle, const ScenePrefabInstantiationSettings& settings) {
    SceneState& state = SceneAccess::State(scene);
    const ScenePrefab* prefab = state.prefabs.Find(handle);
    if (prefab == nullptr) {
        return {};
    }

    const ScenePrefabInstance instance = ScenePrefabInstantiationService::Instantiate(scene, *prefab, settings);
    std::vector<SceneObject> objects = CopyObjects(instance);
    ScenePrefabInstanceHandle instanceHandle = state.prefabInstances.Register(handle, settings.parent, objects);
    return ScenePrefabInstance{ instanceHandle, std::move(objects) };
}

} // namespace kb::scene
