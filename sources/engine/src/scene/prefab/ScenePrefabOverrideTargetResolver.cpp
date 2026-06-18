#include "scene/prefab/ScenePrefabOverrideTargetResolver.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "scene/SceneState.hpp"

#include <span>

namespace kb::scene {

const ScenePrefab* ScenePrefabOverrideTargetResolver::ResolveReadPrefab(const SceneState& state, const ScenePrefabInstanceRecord& instance) noexcept {
    const ScenePrefab* resolved = instance.ResolvedPrefab();
    return resolved == nullptr ? state.prefabs.Find(instance.prefab) : resolved;
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
    const std::span<const SceneObject> objects = instance.Objects();
    if (node == nullptr || nodeIndex >= objects.size()) {
        return {};
    }

    const SceneObject object = objects[nodeIndex];
    if (!object.IsValid() || !scene.Entities().IsAlive(object)) {
        return {};
    }
    return ScenePrefabOverrideNodeTarget{ .node = node, .object = object };
}

} // namespace kb::scene
