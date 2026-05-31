#include "scene/prefab/ScenePrefabOverrideDetector.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/ScenePrefab.hpp"
#include "scene/prefab/ScenePrefabInstanceTopology.hpp"
#include "scene/prefab/ScenePrefabNodeComparator.hpp"
#include "scene/prefab/ScenePrefabOverridePropertyReporter.hpp"

#include <span>

namespace kb::scene {

ScenePrefabOverrideReport ScenePrefabOverrideDetector::Detect(Scene& scene, const ScenePrefab& prefab, const ScenePrefabInstanceRecord& instance) {
    ScenePrefabOverrideReport report;
    const std::span<const ScenePrefabNodeDesc> nodes = prefab.Nodes();
    const ScenePrefabTrackedEntitySet trackedEntities = ScenePrefabInstanceTopology::TrackedEntities(instance);
    for (std::uint32_t nodeIndex = 0; nodeIndex < static_cast<std::uint32_t>(nodes.size()); ++nodeIndex) {
        const SceneObject object = nodeIndex < instance.objects.size() ? instance.objects[nodeIndex] : SceneObject{};
        const SceneObject expectedParent = ScenePrefabInstanceTopology::ExpectedParent(nodes[nodeIndex], instance);
        ScenePrefabOverrideFlag flags = ScenePrefabNodeComparator::Compare(scene, object, expectedParent, nodes[nodeIndex]);
        if (ScenePrefabInstanceTopology::HasUntrackedChild(scene, object, trackedEntities)) {
            flags |= ScenePrefabOverrideFlag::AddedChild;
            ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "children", "added", ScenePrefabOverrideFlag::AddedChild);
        }
        if (flags != ScenePrefabOverrideFlag::None) {
            report.nodes.push_back(ScenePrefabNodeOverride{
                .nodeIndex = nodeIndex,
                .object = object,
                .flags = flags,
            });
            ScenePrefabOverridePropertyReporter::AppendChangedProperties(scene, nodes[nodeIndex], expectedParent, nodeIndex, object, report);
        }
    }
    return report;
}

} // namespace kb::scene
