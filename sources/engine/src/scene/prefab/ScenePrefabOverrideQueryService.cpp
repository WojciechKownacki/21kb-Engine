#include "scene/prefab/ScenePrefabOverrideQueryService.hpp"

#include "engine/scene/Scene.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabOverrideDetector.hpp"
#include "scene/prefab/ScenePrefabOverrideTargetResolver.hpp"

namespace kb::scene {

bool ScenePrefabOverrideQueryService::IsInstance(Scene& scene, ScenePrefabInstanceHandle handle) noexcept {
    const ScenePrefabInstanceRecord* instance = SceneAccess::State(scene).prefabInstances.Find(handle);
    return instance != nullptr && !instance->objects.empty() && instance->objects.front().IsValid();
}

ScenePrefabOverrideReport ScenePrefabOverrideQueryService::Overrides(Scene& scene, ScenePrefabInstanceHandle handle) {
    const SceneState& state = SceneAccess::State(scene);
    const ScenePrefabInstanceRecord* instance = state.prefabInstances.Find(handle);
    if (instance == nullptr) {
        return {};
    }

    const ScenePrefab* prefab = ScenePrefabOverrideTargetResolver::ResolveReadPrefab(state, *instance);
    return prefab == nullptr ? ScenePrefabOverrideReport{} : ScenePrefabOverrideDetector::Detect(scene, *prefab, *instance);
}

} // namespace kb::scene
