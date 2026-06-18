#include "scene/prefab/ScenePrefabInstanceTopology.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"

#include <span>

namespace kb::scene {

SceneObject ScenePrefabInstanceTopology::ExpectedParent(const ScenePrefabNodeDesc& node, const ScenePrefabInstanceRecord& instance) noexcept {
    if (node.parentNode == ScenePrefabNodeDesc::NoParent) {
        return instance.rootParent;
    }
    const std::span<const SceneObject> objects = instance.Objects();
    return node.parentNode < objects.size() ? objects[node.parentNode] : SceneObject{};
}

ScenePrefabTrackedEntitySet ScenePrefabInstanceTopology::TrackedEntities(const ScenePrefabInstanceRecord& instance) {
    ScenePrefabTrackedEntitySet trackedEntities;
    const std::span<const SceneObject> objects = instance.Objects();
    trackedEntities.reserve(objects.size());
    for (const SceneObject object : objects) {
        if (object.IsValid()) {
            trackedEntities.insert(object.Entity().Id());
        }
    }
    return trackedEntities;
}

bool ScenePrefabInstanceTopology::AllTrackedObjectsAlive(Scene& scene, const ScenePrefabInstanceRecord& instance) {
    for (const SceneObject object : instance.Objects()) {
        if (!object.IsValid() || !scene.Entities().IsAlive(object)) {
            return false;
        }
    }
    return true;
}

bool ScenePrefabInstanceTopology::HasUntrackedChild(Scene& scene, SceneObject object, const ScenePrefabTrackedEntitySet& trackedEntities) {
    if (!object.IsValid() || !scene.Entities().IsAlive(object)) {
        return false;
    }

    for (const SceneEntity child : scene.Hierarchy().ChildEntities(object.Entity())) {
        if (!trackedEntities.contains(child.Id())) {
            return true;
        }
    }
    return false;
}

void ScenePrefabInstanceTopology::DestroyUntrackedChildren(Scene& scene, const ScenePrefabInstanceRecord& instance, const ScenePrefabTrackedEntitySet& trackedEntities) {
    for (const SceneObject object : instance.Objects()) {
        if (!object.IsValid() || !scene.Entities().IsAlive(object)) {
            continue;
        }

        for (const SceneEntity child : scene.Hierarchy().ChildEntities(object.Entity())) {
            if (!trackedEntities.contains(child.Id())) {
                scene.Entities().Destroy(child);
            }
        }
    }
}

} // namespace kb::scene
