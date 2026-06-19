#include "scene/prefab/ScenePrefabOverrideReverter.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/ScenePrefab.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabInstanceTopology.hpp"
#include "scene/prefab/ScenePrefabNodeStateWriter.hpp"

#include <span>

namespace kb::scene {

bool ScenePrefabOverrideReverter::Revert(Scene& scene, const ScenePrefab& prefab, ScenePrefabInstanceRecord& instance) {
    const std::span<const ScenePrefabNodeDesc> nodes = prefab.Nodes();
    const std::span<const SceneObject> objects = instance.Objects();
    if (nodes.size() != objects.size()) {
        return false;
    }

    if (!ScenePrefabInstanceTopology::AllTrackedObjectsAlive(scene, instance)) {
        return false;
    }

    const ScenePrefabTrackedEntitySet trackedEntities = ScenePrefabInstanceTopology::TrackedEntities(instance);
    ScenePrefabInstanceTopology::DestroyUntrackedChildren(scene, instance, trackedEntities);
    ScenePrefabNodeStateWriterContext writerContext{ scene, SceneAccess::State(scene) };
    for (std::uint32_t nodeIndex = 0; nodeIndex < static_cast<std::uint32_t>(nodes.size()); ++nodeIndex) {
        ScenePrefabNodeStateWriter::Write(writerContext, objects[nodeIndex], ScenePrefabInstanceTopology::ExpectedParent(nodes[nodeIndex], instance), nodes[nodeIndex]);
    }
    return true;
}

} // namespace kb::scene
