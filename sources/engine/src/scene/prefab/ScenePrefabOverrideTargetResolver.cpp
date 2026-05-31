#include "scene/prefab/ScenePrefabOverrideTargetResolver.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "scene/SceneState.hpp"

namespace kb::scene {

const ScenePrefab* ScenePrefabOverrideTargetResolver::ResolveReadPrefab(const SceneState& state, const ScenePrefabInstanceRecord& instance) noexcept {
    return instance.resolvedPrefab.Empty() ? state.prefabs.Find(instance.prefab) : &instance.resolvedPrefab;
}

ScenePrefabOverrideInstanceTarget ScenePrefabOverrideTargetResolver::ResolveMutablePrefab(SceneState& state, ScenePrefabInstanceHandle handle) noexcept {
    ScenePrefabInstanceRecord* instance = state.prefabInstances.FindMutable(handle);
    if (instance == nullptr) {
        return {};
    }

    ScenePrefab* prefab = state.prefabs.FindMutable(instance->prefab);
    return prefab == nullptr ? ScenePrefabOverrideInstanceTarget{} : ScenePrefabOverrideInstanceTarget{ .instance = instance, .prefab = prefab };
}

ScenePrefabOverrideNodeTarget ScenePrefabOverrideTargetResolver::ResolveNode(Scene& scene, ScenePrefab& prefab, ScenePrefabInstanceRecord& instance, std::uint32_t nodeIndex) {
    ScenePrefabNodeDesc* node = prefab.TryGetMutableNode(nodeIndex);
    if (node == nullptr || nodeIndex >= instance.objects.size()) {
        return {};
    }

    const SceneObject object = instance.objects[nodeIndex];
    if (!object.IsValid() || !scene.Entities().IsAlive(object)) {
        return {};
    }
    return ScenePrefabOverrideNodeTarget{ .node = node, .object = object };
}

} // namespace kb::scene
