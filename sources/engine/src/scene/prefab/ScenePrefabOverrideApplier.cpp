#include "scene/prefab/ScenePrefabOverrideApplier.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/prefab/ScenePrefabComponentSnapshot.hpp"
#include "scene/prefab/ScenePrefabInstanceTopology.hpp"

#include <algorithm>
#include <span>
#include <vector>

namespace kb::scene {
namespace {

void AppendNode(Scene& scene, SceneObject object, std::uint32_t parentNode, ScenePrefab& output, std::vector<SceneObject>& outputObjects) {
    if (!object.IsValid() || !scene.Entities().IsAlive(object)) {
        return;
    }

    const std::uint32_t nodeIndex = output.AddNode(ScenePrefabNodeDesc{
        .name = scene.Entities().Name(object),
        .parentNode = parentNode,
        .transform = scene.Transforms().Get(object),
        .visibility = scene.Components().Visibility().Get(object.Entity()),
        .components = ScenePrefabComponentSnapshot::Capture(scene, object),
    });
    outputObjects.push_back(object);

    for (const SceneEntity child : scene.Hierarchy().ChildEntities(object.Entity())) {
        AppendNode(scene, SceneAccess::MakeObject(scene, child), nodeIndex, output, outputObjects);
    }
}

} // namespace

bool ScenePrefabOverrideApplier::Apply(Scene& scene, ScenePrefab& prefab, ScenePrefabInstanceRecord& instance) {
    if (prefab.NodeCount() != instance.objects.size()) {
        return false;
    }
    if (!ScenePrefabInstanceTopology::AllTrackedObjectsAlive(scene, instance)) {
        return false;
    }

    ScenePrefab updated;
    std::vector<SceneObject> updatedObjects;
    updated.Reserve(instance.objects.size());
    updatedObjects.reserve(instance.objects.size());

    const std::span<const ScenePrefabNodeDesc> nodes = prefab.Nodes();
    for (std::uint32_t index = 0; index < static_cast<std::uint32_t>(nodes.size()); ++index) {
        if (nodes[index].parentNode == ScenePrefabNodeDesc::NoParent) {
            AppendNode(scene, instance.objects[index], ScenePrefabNodeDesc::NoParent, updated, updatedObjects);
        }
    }
    for (const SceneObject object : instance.objects) {
        const auto iterator = std::ranges::find_if(updatedObjects, [object](SceneObject updatedObject) {
            return updatedObject.Entity() == object.Entity();
        });
        if (iterator == updatedObjects.end()) {
            return false;
        }
    }

    prefab = std::move(updated);
    instance.objects = std::move(updatedObjects);
    return true;
}

} // namespace kb::scene
