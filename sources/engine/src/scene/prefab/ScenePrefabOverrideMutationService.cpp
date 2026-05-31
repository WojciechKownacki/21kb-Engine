#include "scene/prefab/ScenePrefabOverrideMutationService.hpp"

#include "engine/scene/Scene.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabOverridePropertyMutationService.hpp"
#include "scene/prefab/ScenePrefabOverrideReverter.hpp"
#include "scene/prefab/ScenePrefabOverrideTargetResolver.hpp"
#include "scene/prefab/ScenePrefabTemplateOverrideService.hpp"
#include "scene/prefab/ScenePrefabVariantOverrideService.hpp"

namespace kb::scene {

bool ScenePrefabOverrideMutationService::Revert(Scene& scene, ScenePrefabInstanceHandle handle) {
    SceneState& state = SceneAccess::State(scene);
    ScenePrefabInstanceRecord* instance = state.prefabInstances.FindMutable(handle);
    if (instance == nullptr) {
        return false;
    }

    const ScenePrefab* prefab = ScenePrefabOverrideTargetResolver::ResolveReadPrefab(state, *instance);
    return prefab != nullptr && ScenePrefabOverrideReverter::Revert(scene, *prefab, *instance);
}

bool ScenePrefabOverrideMutationService::Apply(Scene& scene, ScenePrefabInstanceHandle handle) {
    SceneState& state = SceneAccess::State(scene);
    ScenePrefabInstanceRecord* instance = state.prefabInstances.FindMutable(handle);
    if (instance == nullptr) {
        return false;
    }

    ScenePrefabRecord* record = state.prefabs.FindMutableRecord(instance->prefab);
    if (record == nullptr) {
        return false;
    }
    if (record->kind == ScenePrefabRecordKind::Variant) {
        return ScenePrefabVariantOverrideService::ApplyAll(scene, state.prefabs, *instance, *record);
    }

    return ScenePrefabTemplateOverrideService::ApplyAll(scene, state.prefabs, *instance, *record);
}

bool ScenePrefabOverrideMutationService::RevertProperty(Scene& scene, ScenePrefabInstanceHandle handle, std::uint32_t nodeIndex, std::string_view propertyPath) {
    return ScenePrefabOverridePropertyMutationService::Revert(scene, handle, nodeIndex, propertyPath);
}

bool ScenePrefabOverrideMutationService::ApplyProperty(Scene& scene, ScenePrefabInstanceHandle handle, std::uint32_t nodeIndex, std::string_view propertyPath) {
    return ScenePrefabOverridePropertyMutationService::Apply(scene, handle, nodeIndex, propertyPath);
}

} // namespace kb::scene
