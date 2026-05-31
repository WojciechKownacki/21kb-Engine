#include "scene/prefab/ScenePrefabOverrideDetector.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/ScenePrefab.hpp"
#include "scene/prefab/ScenePrefabInstanceTopology.hpp"
#include "scene/prefab/ScenePrefabNodeComparator.hpp"

#include <span>

namespace kb::scene {

ScenePrefabOverrideReport ScenePrefabOverrideDetector::Detect(Scene& scene, const ScenePrefab& prefab, const ScenePrefabInstanceRecord& instance) {
    ScenePrefabOverrideReport report;
    const std::span<const ScenePrefabNodeDesc> nodes = prefab.Nodes();
    const ScenePrefabTrackedEntitySet trackedEntities = ScenePrefabInstanceTopology::TrackedEntities(instance);
    for (std::uint32_t nodeIndex = 0; nodeIndex < static_cast<std::uint32_t>(nodes.size()); ++nodeIndex) {
        const SceneObject object = nodeIndex < instance.objects.size() ? instance.objects[nodeIndex] : SceneObject{};
        ScenePrefabOverrideFlag flags = ScenePrefabNodeComparator::Compare(scene, object, ScenePrefabInstanceTopology::ExpectedParent(nodes[nodeIndex], instance), nodes[nodeIndex]);
        if (ScenePrefabInstanceTopology::HasUntrackedChild(scene, object, trackedEntities)) {
            flags |= ScenePrefabOverrideFlag::AddedChild;
        }
        if (flags != ScenePrefabOverrideFlag::None) {
            report.nodes.push_back(ScenePrefabNodeOverride{
                .nodeIndex = nodeIndex,
                .object = object,
                .flags = flags,
            });
        }
    }
    return report;
}

} // namespace kb::scene
