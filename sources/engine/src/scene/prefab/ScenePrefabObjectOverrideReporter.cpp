#include "scene/prefab/ScenePrefabObjectOverrideReporter.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/prefab/ScenePrefabOverridePropertyReporter.hpp"
#include "scene/prefab/ScenePrefabOverrideValueFormatter.hpp"

#include <algorithm>
#include <cstddef>

namespace kb::scene {

namespace {

// LIB-092: finds `parent`'s own stable node id WITHIN this instance's node
// list, by matching it against `instanceObjects` (parallel-indexed to
// `instanceNodes`) — the same "which node is this live entity" lookup the
// rest of the reporting pipeline never previously needed, because every
// other override target is already known to be one of the instance's own
// nodes at the point it's reported. A "parent" override's NEW parent is the
// one case that might legitimately be an entity entirely outside this
// instance (returns InvalidStableId/0 then — honestly unresolvable, not a
// bug, mirrors the "children"/AddedChild report-only boundary).
[[nodiscard]] std::uint64_t FindStableNodeId(SceneObject parent, std::span<const SceneObject> instanceObjects, std::span<const ScenePrefabNodeDesc> instanceNodes) {
    if (!parent.IsValid()) {
        return ScenePrefabNodeDesc::InvalidStableId;
    }
    const std::size_t count = std::min(instanceObjects.size(), instanceNodes.size());
    for (std::size_t index = 0; index < count; ++index) {
        if (instanceObjects[index].Entity() == parent.Entity()) {
            return instanceNodes[index].stableId;
        }
    }
    return ScenePrefabNodeDesc::InvalidStableId;
}

} // namespace

bool ScenePrefabObjectOverrideReporter::Append(
    Scene& scene,
    const ScenePrefabNodeDesc& node,
    SceneObject expectedParent,
    std::uint32_t nodeIndex,
    SceneObject object,
    ScenePrefabOverrideReport& report,
    std::span<const SceneObject> instanceObjects,
    std::span<const ScenePrefabNodeDesc> instanceNodes) {
    if (!object.IsValid() || !scene.Entities().IsAlive(object)) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "object", "missing", ScenePrefabOverrideFlag::MissingObject);
        return false;
    }

    const SceneEntity entity = object.Entity();
    if (scene.Entities().Name(object) != node.name) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "name", scene.Entities().Name(object), ScenePrefabOverrideFlag::Name);
    }

    const SceneEntity actualParent = scene.Hierarchy().Parent(entity);
    if (actualParent != expectedParent.Entity()) {
        const SceneObject actualParentObject = SceneAccess::MakeObject(scene, actualParent);
        ScenePrefabOverridePropertyReporter::Add(
            report,
            nodeIndex,
            object,
            "parent",
            ScenePrefabOverrideValueFormatter::ToString(actualParent.Id()),
            ScenePrefabOverrideFlag::Parent,
            actualParentObject,
            0,
            FindStableNodeId(actualParentObject, instanceObjects, instanceNodes));
    }
    return true;
}

} // namespace kb::scene
