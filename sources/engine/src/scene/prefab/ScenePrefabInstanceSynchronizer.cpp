#include "scene/prefab/ScenePrefabInstanceSynchronizer.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabComponentSnapshot.hpp"
#include "scene/prefab/ScenePrefabInstanceRegistry.hpp"
#include "scene/prefab/ScenePrefabInstanceTopology.hpp"
#include "scene/prefab/ScenePrefabNestedResolver.hpp"
#include "scene/prefab/ScenePrefabNodeFactory.hpp"
#include "scene/prefab/ScenePrefabNodeStateWriter.hpp"
#include "scene/prefab/ScenePrefabOverrideDetector.hpp"
#include "scene/prefab/ScenePrefabPropertyOverrideApplier.hpp"
#include "scene/prefab/ScenePrefabPropertyReverter.hpp"
#include "scene/prefab/ScenePrefabRecord.hpp"
#include "scene/prefab/ScenePrefabRegistry.hpp"
#include "scene/prefab/ScenePrefabValidator.hpp"

#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace kb::scene {
namespace {

[[nodiscard]] SceneObject ParentForNode(const ScenePrefabNodeDesc& node, const ScenePrefabInstanceRecord& instance, std::span<const SceneObject> objects) noexcept {
    if (node.parentNode == ScenePrefabNodeDesc::NoParent) {
        return instance.rootParent;
    }
    return node.parentNode < objects.size() ? objects[node.parentNode] : SceneObject{};
}

[[nodiscard]] ScenePrefabNodeDesc SnapshotNode(Scene& scene, SceneObject object, const ScenePrefabNodeDesc& baseline) {
    return ScenePrefabNodeDesc{
        .name = scene.Entities().Name(object),
        .nestedPrefabGuid = baseline.nestedPrefabGuid,
        .nestedPrefabOverrides = baseline.nestedPrefabOverrides,
        .parentNode = baseline.parentNode,
        .transform = scene.Transforms().Get(object),
        .visibility = scene.Components().Visibility().Get(object.Entity()),
        .components = ScenePrefabComponentSnapshot::Capture(scene, object),
    };
}

[[nodiscard]] bool ApplyStoredProperty(Scene& scene, ScenePrefabInstanceRecord& instance, const ScenePrefab& baseline, const ScenePrefabPropertyOverride& property) {
    const std::uint32_t nodeIndex = baseline.ResolveNodeIndex(property);
    const std::span<const SceneObject> readObjects = instance.Objects();
    if (nodeIndex == ScenePrefabNodeDesc::NoParent || nodeIndex >= readObjects.size()) {
        return true;
    }
    if (property.propertyPath == "object") {
        if (property.value == "missing") {
            std::vector<SceneObject>& mutableObjects = instance.MutableObjects();
            SceneObject& object = mutableObjects[nodeIndex];
            if (object.IsValid() && scene.Entities().IsAlive(object)) {
                scene.Entities().Destroy(object);
            }
            object = SceneObject{};
        }
        return true;
    }
    if (property.propertyPath == "children") {
        // LIB-092: added/removed child structure (ScenePrefabOverrideFlag::
        // AddedChild, detected by ScenePrefabInstanceTopology::
        // HasUntrackedChild) is INTENTIONALLY diff/report-only — it is
        // never structurally reapplied here, in memory or from disk. This
        // mirrors LIB-070's own scoping note that ScenePrefabOverride* is
        // "editor diff+revert tooling," not a full structural sync engine.
        // Unlike the "parent" override fixed above (reparenting an
        // EXISTING tracked node), reproducing an added/removed child would
        // mean creating or destroying entities during override apply — a
        // materially larger, not-yet-scoped feature.
        return true;
    }

    SceneObject object = readObjects[nodeIndex];
    if (!object.IsValid() || !scene.Entities().IsAlive(object)) {
        return false;
    }
    if (property.propertyPath == "parent") {
        // LIB-092: prefer the stable, within-instance node reference over
        // the raw `objectReference` SceneObject — the latter is only ever
        // valid for the SAME live session that detected the override (it
        // does not survive a save/load round trip). objectReferenceNodeId
        // resolves against THIS instantiation's own objects, exactly like
        // `target`/nodeIndex already do via baseline.ResolveNodeIndex.
        // Falls back to property.objectReference when unresolvable (the
        // new parent lies outside this instance — not portably
        // referenceable from the prefab asset, or the override predates
        // this field) — the same honest limitation as before this fix.
        SceneObject newParent = property.objectReference;
        if (property.objectReferenceNodeId != ScenePrefabNodeDesc::InvalidStableId) {
            const std::uint32_t parentNodeIndex = baseline.FindNodeIndexByStableId(property.objectReferenceNodeId);
            if (parentNodeIndex != ScenePrefabNodeDesc::NoParent && parentNodeIndex < readObjects.size()) {
                newParent = readObjects[parentNodeIndex];
            }
        }
        return scene.Hierarchy().SetParent(object, newParent);
    }

    const ScenePrefabNodeDesc* baselineNode = baseline.TryGetNode(nodeIndex);
    if (baselineNode == nullptr) {
        return true;
    }

    ScenePrefabNodeDesc editedNode = SnapshotNode(scene, object, *baselineNode);
    if (!ScenePrefabPropertyOverrideApplier::Apply(editedNode, property)) {
        return false;
    }

    return ScenePrefabPropertyReverter::Revert(scene, instance, object, editedNode, property.propertyPath);
}

[[nodiscard]] bool ApplyStoredProperties(Scene& scene, ScenePrefabInstanceRecord& instance, const ScenePrefab& baseline, const ScenePrefabOverrideReport& report) {
    for (const ScenePrefabPropertyOverride& property : report.properties) {
        if (!ApplyStoredProperty(scene, instance, baseline, property)) {
            return false;
        }
    }
    return true;
}

void DestroyRemovedObjects(Scene& scene, std::span<const SceneObject> oldObjects, std::span<const SceneObject> newObjects) {
    std::unordered_set<SceneEntity::IdType> retainedEntities;
    retainedEntities.reserve(newObjects.size());
    for (const SceneObject object : newObjects) {
        if (object.IsValid()) {
            retainedEntities.insert(object.Entity().Id());
        }
    }

    for (const SceneObject object : oldObjects) {
        if (object.IsValid() && scene.Entities().IsAlive(object) && !retainedEntities.contains(object.Entity().Id())) {
            scene.Entities().Destroy(object);
        }
    }
}

[[nodiscard]] std::unordered_map<std::uint64_t, SceneObject> BuildStableObjectMap(const ScenePrefab& baseline, std::span<const SceneObject> objects) {
    std::unordered_map<std::uint64_t, SceneObject> stableObjects;
    const std::span<const ScenePrefabNodeDesc> nodes = baseline.Nodes();
    stableObjects.reserve(nodes.size());
    for (std::uint32_t nodeIndex = 0; nodeIndex < static_cast<std::uint32_t>(nodes.size()) && nodeIndex < objects.size(); ++nodeIndex) {
        const std::uint64_t stableId = nodes[nodeIndex].stableId;
        if (stableId != ScenePrefabNodeDesc::InvalidStableId) {
            stableObjects.emplace(stableId, objects[nodeIndex]);
        }
    }
    return stableObjects;
}

[[nodiscard]] bool RebuildTrackedObjects(Scene& scene, ScenePrefabInstanceRecord& instance, const ScenePrefab& previousBaseline, const ScenePrefab& nextBaseline) {
    const std::span<const SceneObject> currentObjects = instance.Objects();
    const std::vector<SceneObject> oldObjects{ currentObjects.begin(), currentObjects.end() };
    const std::unordered_map<std::uint64_t, SceneObject> stableObjects = BuildStableObjectMap(previousBaseline, oldObjects);
    const std::span<const ScenePrefabNodeDesc> nodes = nextBaseline.Nodes();
    std::vector<SceneObject> rebuiltObjects;
    rebuiltObjects.reserve(nodes.size());
    ScenePrefabNodeStateWriterContext writerContext{ scene, SceneAccess::State(scene) };

    for (std::uint32_t nodeIndex = 0; nodeIndex < static_cast<std::uint32_t>(nodes.size()); ++nodeIndex) {
        const ScenePrefabNodeDesc& node = nodes[nodeIndex];
        SceneObject object;
        const auto stableObject = stableObjects.find(node.stableId);
        if (stableObject != stableObjects.end()) {
            object = stableObject->second;
        }
        if (object.IsValid() && scene.Entities().IsAlive(object)) {
            ScenePrefabNodeStateWriter::Write(writerContext, object, ParentForNode(node, instance, rebuiltObjects), node);
        } else {
            object = ScenePrefabNodeFactory::Create(
                scene,
                node,
                ScenePrefabInstantiationSettings{
                    .parent = instance.rootParent,
                    .namePrefix = {},
                },
                rebuiltObjects);
        }
        rebuiltObjects.push_back(object);
    }

    DestroyRemovedObjects(scene, oldObjects, rebuiltObjects);
    instance.SetObjects(std::move(rebuiltObjects));
    return true;
}

[[nodiscard]] std::size_t RefreshExact(Scene& scene, ScenePrefabRegistry& registry, ScenePrefabInstanceRegistry& instances, ScenePrefabHandle handle) {
    std::size_t refreshedCount = 0;
    const std::vector<ScenePrefabInstanceHandle> handles = instances.HandlesForPrefab(handle);
    for (const ScenePrefabInstanceHandle instanceHandle : handles) {
        ScenePrefabInstanceRecord* instance = instances.FindMutable(instanceHandle);
        if (instance == nullptr) {
            continue;
        }

        const std::span<const SceneObject> currentObjects = instance->Objects();
        const std::vector<SceneObject> oldObjects{ currentObjects.begin(), currentObjects.end() };
        if (ScenePrefabInstanceSynchronizer::RefreshInstance(scene, registry, *instance)) {
            ++refreshedCount;
        }
        instances.ReindexObjects(instanceHandle, oldObjects);
    }
    return refreshedCount;
}

[[nodiscard]] std::size_t RefreshRecursive(Scene& scene, ScenePrefabRegistry& registry, ScenePrefabInstanceRegistry& instances, ScenePrefabHandle handle) {
    std::size_t refreshedCount = RefreshExact(scene, registry, instances, handle);
    for (const ScenePrefabHandle child : registry.VariantChildrenOf(handle)) {
        refreshedCount += RefreshRecursive(scene, registry, instances, child);
    }
    return refreshedCount;
}

} // namespace

std::size_t ScenePrefabInstanceSynchronizer::Refresh(Scene& scene, ScenePrefabHandle handle) {
    if (!handle.IsValid()) {
        return 0;
    }

    SceneState& state = SceneAccess::State(scene);
    if (state.prefabs.FindRecord(handle) == nullptr) {
        return 0;
    }

    return RefreshRecursive(scene, state.prefabs, state.prefabInstances, handle);
}

bool ScenePrefabInstanceSynchronizer::RefreshInstance(Scene& scene, ScenePrefabRegistry& registry, ScenePrefabInstanceRecord& instance) {
    const ScenePrefabRecord* record = registry.FindRecord(instance.prefab);
    if (record == nullptr) {
        return false;
    }

    const ScenePrefab previousBaseline = instance.BaselineOr(record->prefab);
    if (!ScenePrefabValidator::IsValid(previousBaseline)) {
        return false;
    }

    const ScenePrefabOverrideReport preservedOverrides = ScenePrefabOverrideDetector::Detect(scene, previousBaseline, instance);
    ScenePrefab nextBaseline = ScenePrefabNestedResolver::Resolve(registry, record->prefab);
    if (!ScenePrefabValidator::IsValid(nextBaseline) || nextBaseline.Empty()) {
        return false;
    }

    if (!RebuildTrackedObjects(scene, instance, previousBaseline, nextBaseline)) {
        return false;
    }

    instance.SetResolvedPrefab(nextBaseline);
    return ApplyStoredProperties(scene, instance, nextBaseline, preservedOverrides);
}

} // namespace kb::scene
