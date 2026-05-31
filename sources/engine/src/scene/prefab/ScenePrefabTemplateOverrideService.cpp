#include "scene/prefab/ScenePrefabTemplateOverrideService.hpp"

#include "scene/prefab/ScenePrefabNestedResolver.hpp"
#include "scene/prefab/ScenePrefabOverrideApplier.hpp"
#include "scene/prefab/ScenePrefabPropertyMutator.hpp"

namespace kb::scene {

bool ScenePrefabTemplateOverrideService::ApplyAll(Scene& scene, ScenePrefabRegistry& registry, ScenePrefabInstanceRecord& instance, ScenePrefabRecord& record) {
    if (!ScenePrefabOverrideApplier::Apply(scene, record.prefab, instance)) {
        return false;
    }

    registry.RefreshContentHash(instance.prefab);
    registry.RefreshDerivedPrefabs(instance.prefab);
    instance.resolvedPrefab = ScenePrefabNestedResolver::Resolve(registry, record.prefab);
    return true;
}

bool ScenePrefabTemplateOverrideService::ApplyChildren(Scene& scene, ScenePrefabRegistry& registry, ScenePrefabInstanceRecord& instance, ScenePrefab& prefab) {
    if (!ScenePrefabOverrideApplier::Apply(scene, prefab, instance)) {
        return false;
    }

    registry.RefreshContentHash(instance.prefab);
    return true;
}

bool ScenePrefabTemplateOverrideService::ApplyProperty(Scene& scene, ScenePrefabRegistry& registry, ScenePrefabInstanceRecord& instance, ScenePrefabNodeDesc& node, SceneObject object, std::string_view propertyPath) {
    if (!ScenePrefabPropertyMutator::Apply(scene, node, object, propertyPath)) {
        return false;
    }

    registry.RefreshContentHash(instance.prefab);
    registry.RefreshDerivedPrefabs(instance.prefab);
    const ScenePrefab* refreshed = registry.Find(instance.prefab);
    if (refreshed != nullptr) {
        instance.resolvedPrefab = ScenePrefabNestedResolver::Resolve(registry, *refreshed);
    }
    return true;
}

} // namespace kb::scene
