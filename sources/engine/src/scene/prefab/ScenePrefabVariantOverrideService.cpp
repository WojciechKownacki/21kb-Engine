#include "scene/prefab/ScenePrefabVariantOverrideService.hpp"

#include "engine/scene/ScenePrefabOverrides.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/ScenePrefabCaptureSettings.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/prefab/ScenePrefabAppliedPropertyBuilder.hpp"
#include "scene/prefab/ScenePrefabCaptureService.hpp"
#include "scene/prefab/ScenePrefabInstanceSynchronizer.hpp"
#include "scene/prefab/ScenePrefabInstanceTopology.hpp"
#include "scene/prefab/ScenePrefabNestedResolver.hpp"
#include "scene/prefab/ScenePrefabOverrideDetector.hpp"
#include "scene/prefab/ScenePrefabPropertyOverrideApplier.hpp"
#include "scene/prefab/ScenePrefabRegistry.hpp"

#include <algorithm>
#include <span>
#include <utility>
#include <vector>

namespace kb::scene {
namespace {

[[nodiscard]] bool RefreshResolvedPrefab(ScenePrefabRegistry& registry, ScenePrefabInstanceRecord& instance) {
    const ScenePrefab* refreshed = registry.Find(instance.prefab);
    if (refreshed == nullptr) {
        return false;
    }

    instance.SetResolvedPrefab(ScenePrefabNestedResolver::Resolve(registry, *refreshed));
    return true;
}

} // namespace

bool ScenePrefabVariantOverrideService::ApplyAll(Scene& scene, ScenePrefabRegistry& registry, ScenePrefabInstanceRecord& instance, ScenePrefabRecord& variantRecord) {
    const ScenePrefabOverrideReport report = ScenePrefabOverrideDetector::Detect(scene, variantRecord.prefab, instance);
    for (const ScenePrefabPropertyOverride& property : report.properties) {
        // LIB-092: the "children"/"added" marker is now promoted structurally
        // via variantAddedChildren below (a captured subtree), not as a
        // report-only string override that the materializer/synchronizer
        // would no-op — so it is deliberately NOT upserted into the property
        // list here.
        if (property.propertyPath == "children") {
            continue;
        }
        if (!registry.UpsertVariantOverride(instance.prefab, property)) {
            return false;
        }
    }

    // LIB-092: PROMOTE this instance's added-child subtrees into the variant,
    // exactly as ApplyOverrides on a TEMPLATE already re-captures added
    // children into its base prefab (ScenePrefabOverrideApplier). Each
    // untracked child of an AddedChild host node is captured whole
    // (ScenePrefabCaptureService) and ACCUMULATED onto the variant's stored
    // subtrees — accumulate, not replace, because a child promoted by an
    // earlier ApplyOverrides is now a tracked node (not re-detected here), so
    // replacing would drop it. The variant materializer re-appends the whole
    // list, so the variant prefab (and every future instance, in memory and
    // from disk) reproduces the added children.
    const ScenePrefabTrackedEntitySet trackedEntities = ScenePrefabInstanceTopology::TrackedEntities(instance);
    const std::span<const SceneObject> objects = instance.Objects();
    std::vector<ScenePrefabVariantAddedSubtree> accumulatedAddedChildren = variantRecord.variantAddedChildren;
    bool capturedAny = false;
    for (const ScenePrefabNodeOverride& node : report.nodes) {
        if (!HasPrefabOverride(node.flags, ScenePrefabOverrideFlag::AddedChild) || node.nodeIndex >= objects.size()) {
            continue;
        }
        const SceneObject hostObject = objects[node.nodeIndex];
        if (!hostObject.IsValid() || !scene.Entities().IsAlive(hostObject)) {
            continue;
        }
        for (const SceneEntity child : scene.Hierarchy().ChildEntities(hostObject.Entity())) {
            if (std::ranges::binary_search(trackedEntities, child.Id())) {
                continue;
            }
            ScenePrefab subtree = ScenePrefabCaptureService::Capture(scene, SceneAccess::MakeObject(scene, child), ScenePrefabCaptureSettings{ .includeChildren = true });
            if (subtree.Empty()) {
                continue;
            }
            accumulatedAddedChildren.push_back(ScenePrefabVariantAddedSubtree{ .hostNodeId = node.nodeId, .subtree = std::move(subtree) });
            capturedAny = true;
        }
    }
    if (capturedAny && !registry.SetVariantAddedChildren(instance.prefab, std::move(accumulatedAddedChildren))) {
        return false;
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
    const ScenePrefab* resolvedPrefab = instance.ResolvedPrefab();
    if (const ScenePrefabNodeDesc* node = resolvedPrefab == nullptr ? nullptr : resolvedPrefab->TryGetNode(nodeIndex); node != nullptr) {
        property.nodeId = node->stableId;
    } else {
        const std::span<const std::uint64_t> nodeIds = instance.NodeIds();
        if (nodeIndex < nodeIds.size()) {
            property.nodeId = nodeIds[nodeIndex];
        }
    }
    const bool audioSourceProperty = propertyPath == "audioSource" || propertyPath.starts_with("audioSource.");
    if (audioSourceProperty) {
        if (resolvedPrefab == nullptr) {
            return false;
        }
        const ScenePrefabNodeDesc* resolvedNode = resolvedPrefab->TryGetNode(nodeIndex);
        if (resolvedNode == nullptr) {
            return false;
        }
        ScenePrefabNodeDesc candidate = *resolvedNode;
        if (!ScenePrefabPropertyOverrideApplier::Apply(candidate, property)
            || (candidate.components.audioSource.has_value()
                && !IsAudioSourceComponentPersistable(*candidate.components.audioSource))) {
            return false;
        }
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
