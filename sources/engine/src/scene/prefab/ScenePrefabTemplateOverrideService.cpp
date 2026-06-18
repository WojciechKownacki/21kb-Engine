#include "scene/prefab/ScenePrefabTemplateOverrideService.hpp"

#include "scene/prefab/ScenePrefabNestedResolver.hpp"
#include "scene/prefab/ScenePrefabHasher.hpp"
#include "scene/prefab/ScenePrefabInstanceSynchronizer.hpp"
#include "scene/prefab/ScenePrefabOverrideApplier.hpp"
#include "scene/prefab/ScenePrefabPropertyMutator.hpp"

namespace kb::scene {

bool ScenePrefabTemplateOverrideService::ApplyAll(Scene& scene, ScenePrefabRegistry& registry, ScenePrefabInstanceRecord& instance, ScenePrefabRecord& record) {
    const std::uint64_t beforeHash = ScenePrefabHasher::Hash(record.prefab);
    if (!ScenePrefabOverrideApplier::Apply(scene, record.prefab, instance)) {
        return false;
    }
    if (ScenePrefabHasher::Hash(record.prefab) == beforeHash) {
        return true;
    }

    registry.RefreshContentHash(instance.prefab);
    registry.RefreshDerivedPrefabs(instance.prefab);
    instance.SetResolvedPrefab(ScenePrefabNestedResolver::Resolve(registry, record.prefab));
    static_cast<void>(ScenePrefabInstanceSynchronizer::Refresh(scene, instance.prefab));
    return true;
}

bool ScenePrefabTemplateOverrideService::ApplyChildren(Scene& scene, ScenePrefabRegistry& registry, ScenePrefabInstanceRecord& instance, ScenePrefab& prefab) {
    const std::uint64_t beforeHash = ScenePrefabHasher::Hash(prefab);
    if (!ScenePrefabOverrideApplier::Apply(scene, prefab, instance)) {
        return false;
    }
    if (ScenePrefabHasher::Hash(prefab) == beforeHash) {
        return true;
    }

    registry.RefreshContentHash(instance.prefab);
    registry.RefreshDerivedPrefabs(instance.prefab);
    static_cast<void>(ScenePrefabInstanceSynchronizer::Refresh(scene, instance.prefab));
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
        instance.SetResolvedPrefab(ScenePrefabNestedResolver::Resolve(registry, *refreshed));
    }
    static_cast<void>(ScenePrefabInstanceSynchronizer::Refresh(scene, instance.prefab));
    return true;
}

} // namespace kb::scene
