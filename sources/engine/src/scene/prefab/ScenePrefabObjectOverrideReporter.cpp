#include "scene/prefab/ScenePrefabObjectOverrideReporter.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/prefab/ScenePrefabOverridePropertyReporter.hpp"
#include "scene/prefab/ScenePrefabOverrideValueFormatter.hpp"

namespace kb::scene {

bool ScenePrefabObjectOverrideReporter::Append(Scene& scene, const ScenePrefabNodeDesc& node, SceneObject expectedParent, std::uint32_t nodeIndex, SceneObject object, ScenePrefabOverrideReport& report) {
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
        ScenePrefabOverridePropertyReporter::Add(
            report,
            nodeIndex,
            object,
            "parent",
            ScenePrefabOverrideValueFormatter::ToString(actualParent.Id()),
            ScenePrefabOverrideFlag::Parent,
            SceneAccess::MakeObject(scene, actualParent));
    }
    return true;
}

} // namespace kb::scene
