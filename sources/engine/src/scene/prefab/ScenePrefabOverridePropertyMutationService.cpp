#include "scene/prefab/ScenePrefabOverridePropertyMutationService.hpp"

#include "engine/scene/Scene.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabInstanceTopology.hpp"
#include "scene/prefab/ScenePrefabOverrideTargetResolver.hpp"
#include "scene/prefab/ScenePrefabPropertyMutator.hpp"
#include "scene/prefab/ScenePrefabTemplateOverrideService.hpp"
#include "scene/prefab/ScenePrefabVariantOverrideService.hpp"

namespace kb::scene {

bool ScenePrefabOverridePropertyMutationService::Revert(Scene& scene, ScenePrefabInstanceHandle handle, std::uint32_t nodeIndex, std::string_view propertyPath) {
    SceneState& state = SceneAccess::State(scene);
    ScenePrefabOverrideInstanceTarget target = ScenePrefabOverrideTargetResolver::ResolveMutablePrefab(state, handle);
    if (target.instance == nullptr || target.prefab == nullptr) {
        return false;
    }
    if (propertyPath == "children") {
        const ScenePrefabTrackedEntitySet trackedEntities = ScenePrefabInstanceTopology::TrackedEntities(*target.instance);
        ScenePrefabInstanceTopology::DestroyUntrackedChildren(scene, *target.instance, trackedEntities);
        return true;
    }

    const ScenePrefabOverrideNodeTarget nodeTarget = ScenePrefabOverrideTargetResolver::ResolveNode(scene, *target.prefab, *target.instance, nodeIndex);
    return nodeTarget.node != nullptr && ScenePrefabPropertyMutator::Revert(scene, *target.instance, nodeTarget.object, *nodeTarget.node, propertyPath);
}

bool ScenePrefabOverridePropertyMutationService::Apply(Scene& scene, ScenePrefabInstanceHandle handle, std::uint32_t nodeIndex, std::string_view propertyPath) {
    SceneState& state = SceneAccess::State(scene);
    ScenePrefabOverrideInstanceTarget target = ScenePrefabOverrideTargetResolver::ResolveMutablePrefab(state, handle);
    if (target.instance == nullptr || target.prefab == nullptr) {
        return false;
    }
    if (propertyPath == "children") {
        return ScenePrefabTemplateOverrideService::ApplyChildren(scene, state.prefabs, *target.instance, *target.prefab);
    }

    const ScenePrefabOverrideNodeTarget nodeTarget = ScenePrefabOverrideTargetResolver::ResolveNode(scene, *target.prefab, *target.instance, nodeIndex);
    if (nodeTarget.node == nullptr) {
        return false;
    }

    ScenePrefabRecord* record = state.prefabs.FindMutableRecord(target.instance->prefab);
    if (record != nullptr && record->kind == ScenePrefabRecordKind::Variant) {
        return ScenePrefabVariantOverrideService::ApplyProperty(scene, state.prefabs, *target.instance, nodeIndex, nodeTarget.object, propertyPath);
    }

    return ScenePrefabTemplateOverrideService::ApplyProperty(scene, state.prefabs, *target.instance, *nodeTarget.node, nodeTarget.object, propertyPath);
}

} // namespace kb::scene
