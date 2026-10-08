#include "scene/prefab/ScenePrefabOverrideApplier.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabComponentSnapshot.hpp"
#include "scene/prefab/ScenePrefabInstanceTopology.hpp"
#include "scene/prefab/ScenePrefabNestedResolver.hpp"
#include "scene/prefab/ScenePrefabOverrideDetector.hpp"
#include "scene/prefab/ScenePrefabRecord.hpp"

#include <algorithm>
#include <span>
#include <vector>

namespace kb::scene {
namespace {

[[nodiscard]] std::uint32_t FindTrackedObjectIndex(const ScenePrefabInstanceRecord& instance, SceneObject object) noexcept {
    const std::span<const SceneObject> objects = instance.Objects();
    for (std::uint32_t index = 0; index < static_cast<std::uint32_t>(objects.size()); ++index) {
        if (objects[index].Entity() == object.Entity()) {
            return index;
        }
    }
    return ScenePrefabNodeDesc::NoParent;
}

void AppendNode(
    Scene& scene,
    SceneObject object,
    std::uint32_t parentNode,
    const ScenePrefab& sourcePrefab,
    const ScenePrefabInstanceRecord& sourceInstance,
    ScenePrefab& output,
    std::vector<SceneObject>& outputObjects) {
    if (!object.IsValid() || !scene.Entities().IsAlive(object)) {
        return;
    }

    std::uint64_t stableId = ScenePrefabNodeDesc::InvalidStableId;
    const std::uint32_t sourceNodeIndex = FindTrackedObjectIndex(sourceInstance, object);
    const ScenePrefabNodeDesc* sourceNode = sourcePrefab.TryGetNode(sourceNodeIndex);
    if (sourceNode != nullptr) {
        stableId = sourceNode->stableId;
    }

    const std::uint32_t nodeIndex = output.AddNode(ScenePrefabNodeDesc{
        .stableId = stableId,
        .name = scene.Entities().Name(object),
        .nestedPrefabGuid = sourceNode != nullptr ? sourceNode->nestedPrefabGuid : std::string{},
        .nestedPrefabOverrides = sourceNode != nullptr ? sourceNode->nestedPrefabOverrides : std::vector<ScenePrefabPropertyOverride>{},
        .parentNode = parentNode,
        .transform = scene.Transforms().Get(object),
        .visibility = scene.Components().Visibility().Get(object.Entity()),
        .components = ScenePrefabComponentSnapshot::Capture(scene, object),
    });
    outputObjects.push_back(object);

    for (const SceneEntity child : scene.Hierarchy().ChildEntities(object.Entity())) {
        AppendNode(scene, SceneAccess::MakeObject(scene, child), nodeIndex, sourcePrefab, sourceInstance, output, outputObjects);
    }
}

// A node that nests another prefab keeps the link, and its overrides become what its live subtree now
// changes in that prefab. The subtree is matched to the nested prefab in depth-first order, as the
// nested resolver maps it.
void RecordNestedOverrides(Scene& scene, ScenePrefab& prefab, std::span<const SceneObject> objects, SceneObject rootParent) {
    const ScenePrefabRegistry& registry = SceneAccess::State(scene).prefabs;
    const std::uint32_t nodeCount = static_cast<std::uint32_t>(std::min(prefab.NodeCount(), objects.size()));
    for (std::uint32_t root = 0U; root < nodeCount; ++root) {
        const std::string guid = prefab.Nodes()[root].nestedPrefabGuid;
        const ScenePrefabRecord* record = guid.empty() ? nullptr : registry.FindRecord(registry.FindByGuid(guid));
        if (record == nullptr) {
            continue;
        }
        const ScenePrefab nested = ScenePrefabNestedResolver::Resolve(registry, record->prefab);
        std::uint32_t end = root + 1U;
        while (end < nodeCount && prefab.Nodes()[end].parentNode >= root && prefab.Nodes()[end].parentNode < end) {
            ++end;
        }
        if (end - root < nested.NodeCount()) {
            continue;
        }
        const std::uint32_t parentNode = prefab.Nodes()[root].parentNode;
        ScenePrefabInstanceRecord subtree{ .rootParent = parentNode < nodeCount ? objects[parentNode] : rootParent };
        subtree.SetObjects(std::vector<SceneObject>{ objects.begin() + root, objects.begin() + root + static_cast<std::ptrdiff_t>(nested.NodeCount()) });
        prefab.TryGetMutableNode(root)->nestedPrefabOverrides = ScenePrefabOverrideDetector::Detect(scene, nested, subtree).properties;
    }
}

} // namespace

bool ScenePrefabOverrideApplier::Apply(Scene& scene, ScenePrefab& prefab, ScenePrefabInstanceRecord& instance) {
    const std::span<const SceneObject> objects = instance.Objects();
    if (prefab.NodeCount() != objects.size()) {
        return false;
    }

    ScenePrefab updated;
    std::vector<SceneObject> updatedObjects;
    updated.Reserve(objects.size());
    updatedObjects.reserve(objects.size());

    const std::span<const ScenePrefabNodeDesc> nodes = prefab.Nodes();
    for (std::uint32_t index = 0; index < static_cast<std::uint32_t>(nodes.size()); ++index) {
        if (nodes[index].parentNode == ScenePrefabNodeDesc::NoParent) {
            if (index >= objects.size() || !objects[index].IsValid() || !scene.Entities().IsAlive(objects[index])) {
                return false;
            }
            AppendNode(scene, objects[index], ScenePrefabNodeDesc::NoParent, prefab, instance, updated, updatedObjects);
        }
    }
    std::vector<SceneEntity::IdType> updatedEntityIds;
    updatedEntityIds.reserve(updatedObjects.size());
    for (const SceneObject object : updatedObjects) {
        if (object.IsValid()) {
            updatedEntityIds.push_back(object.Entity().Id());
        }
    }
    std::ranges::sort(updatedEntityIds);
    const auto uniqueEnd = std::ranges::unique(updatedEntityIds).begin();
    updatedEntityIds.erase(uniqueEnd, updatedEntityIds.end());

    for (const SceneObject object : objects) {
        if (!object.IsValid() || !scene.Entities().IsAlive(object)) {
            continue;
        }

        if (!std::ranges::binary_search(updatedEntityIds, object.Entity().Id())) {
            return false;
        }
    }

    RecordNestedOverrides(scene, updated, updatedObjects, instance.rootParent);
    prefab = std::move(updated);
    instance.SetObjects(std::move(updatedObjects));
    return true;
}

} // namespace kb::scene
