#include "scene/prefab/ScenePrefabOverrideService.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabOverrideApplier.hpp"
#include "scene/prefab/ScenePrefabOverrideDetector.hpp"
#include "scene/prefab/ScenePrefabOverrideReverter.hpp"

namespace kb::scene {

bool ScenePrefabOverrideService::IsInstance(Scene& scene, ScenePrefabInstanceHandle handle) noexcept {
    return SceneAccess::State(scene).prefabInstances.Contains(handle);
}

ScenePrefabOverrideReport ScenePrefabOverrideService::Overrides(Scene& scene, ScenePrefabInstanceHandle handle) {
    const SceneState& state = SceneAccess::State(scene);
    const ScenePrefabInstanceRecord* instance = state.prefabInstances.Find(handle);
    if (instance == nullptr) {
        return {};
    }

    const ScenePrefab* prefab = state.prefabs.Find(instance->prefab);
    return prefab == nullptr ? ScenePrefabOverrideReport{} : ScenePrefabOverrideDetector::Detect(scene, *prefab, *instance);
}

bool ScenePrefabOverrideService::Revert(Scene& scene, ScenePrefabInstanceHandle handle) {
    SceneState& state = SceneAccess::State(scene);
    ScenePrefabInstanceRecord* instance = state.prefabInstances.FindMutable(handle);
    if (instance == nullptr) {
        return false;
    }

    const ScenePrefab* prefab = state.prefabs.Find(instance->prefab);
    return prefab != nullptr && ScenePrefabOverrideReverter::Revert(scene, *prefab, *instance);
}

bool ScenePrefabOverrideService::Apply(Scene& scene, ScenePrefabInstanceHandle handle) {
    SceneState& state = SceneAccess::State(scene);
    ScenePrefabInstanceRecord* instance = state.prefabInstances.FindMutable(handle);
    if (instance == nullptr) {
        return false;
    }

    ScenePrefab* prefab = state.prefabs.FindMutable(instance->prefab);
    if (prefab == nullptr || !ScenePrefabOverrideApplier::Apply(scene, *prefab, *instance)) {
        return false;
    }

    state.prefabs.RefreshContentHash(instance->prefab);
    return true;
}

} // namespace kb::scene
