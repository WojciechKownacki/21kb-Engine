#include "scene/prefab/ScenePrefabVariantOverrideService.hpp"

#include "engine/scene/ScenePrefabOverrides.hpp"
#include "scene/prefab/ScenePrefabAppliedPropertyBuilder.hpp"
#include "scene/prefab/ScenePrefabInstanceSynchronizer.hpp"
#include "scene/prefab/ScenePrefabNestedResolver.hpp"
#include "scene/prefab/ScenePrefabOverrideDetector.hpp"
#include "scene/prefab/ScenePrefabRegistry.hpp"

#include <utility>

namespace kb::scene {
namespace {

[[nodiscard]] bool RefreshResolvedPrefab(ScenePrefabRegistry& registry, ScenePrefabInstanceRecord& instance) {
    const ScenePrefab* refreshed = registry.Find(instance.prefab);
    if (refreshed == nullptr) {
        return false;
    }

    instance.resolvedPrefab = ScenePrefabNestedResolver::Resolve(registry, *refreshed);
    return true;
}

} // namespace

bool ScenePrefabVariantOverrideService::ApplyAll(Scene& scene, ScenePrefabRegistry& registry, ScenePrefabInstanceRecord& instance, ScenePrefabRecord& variantRecord) {
    const ScenePrefabOverrideReport report = ScenePrefabOverrideDetector::Detect(scene, variantRecord.prefab, instance);
    for (const ScenePrefabPropertyOverride& property : report.properties) {
        if (!registry.UpsertVariantOverride(instance.prefab, property)) {
            return false;
        }
    }
    if (!RefreshResolvedPrefab(registry, instance)) {
        return false;
    }
    static_cast<void>(ScenePrefabInstanceSynchronizer::Refresh(scene, instance.prefab));
    return true;
}

bool ScenePrefabVariantOverrideService::ApplyProperty(Scene& scene, ScenePrefabRegistry& registry, ScenePrefabInstanceRecord& instance, std::uint32_t nodeIndex, SceneObject object, std::string_view propertyPath) {
    ScenePrefabPropertyOverride property;
    if (!ScenePrefabAppliedPropertyBuilder::Build(scene, nodeIndex, object, propertyPath, property)) {
        return false;
    }
    if (!registry.UpsertVariantOverride(instance.prefab, std::move(property))) {
        return false;
    }
    if (!RefreshResolvedPrefab(registry, instance)) {
        return false;
    }
    static_cast<void>(ScenePrefabInstanceSynchronizer::Refresh(scene, instance.prefab));
    return true;
}

} // namespace kb::scene
