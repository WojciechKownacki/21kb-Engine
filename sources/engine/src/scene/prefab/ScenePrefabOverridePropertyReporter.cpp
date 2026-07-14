#include "scene/prefab/ScenePrefabOverridePropertyReporter.hpp"

#include "engine/scene/Scene.hpp"
#include "scene/prefab/ScenePrefabComponentOverrideReporter.hpp"
#include "scene/prefab/ScenePrefabObjectOverrideReporter.hpp"
#include "scene/prefab/ScenePrefabTransformOverrideReporter.hpp"

#include <cstddef>
#include <span>
#include <utility>

namespace kb::scene {

void ScenePrefabOverridePropertyReporter::Add(
    ScenePrefabOverrideReport& report,
    std::uint32_t nodeIndex,
    SceneObject target,
    std::string propertyPath,
    std::string value,
    ScenePrefabOverrideFlag flag,
    SceneObject objectReference,
    std::uint64_t nodeId,
    std::uint64_t objectReferenceNodeId) {
    report.properties.push_back(ScenePrefabPropertyOverride{
        .nodeIndex = nodeIndex,
        .nodeId = nodeId,
        .target = target,
        .propertyPath = std::move(propertyPath),
        .value = std::move(value),
        .objectReference = objectReference,
        .objectReferenceNodeId = objectReferenceNodeId,
        .flag = flag,
    });
}

void ScenePrefabOverridePropertyReporter::AppendChangedProperties(
    Scene& scene,
    const ScenePrefabNodeDesc& node,
    SceneObject expectedParent,
    std::uint32_t nodeIndex,
    SceneObject object,
    ScenePrefabOverrideReport& report,
    std::span<const SceneObject> instanceObjects,
    std::span<const ScenePrefabNodeDesc> instanceNodes) {
    const std::size_t firstProperty = report.properties.size();
    if (!ScenePrefabObjectOverrideReporter::Append(scene, node, expectedParent, nodeIndex, object, report, instanceObjects, instanceNodes)) {
        for (std::size_t propertyIndex = firstProperty; propertyIndex < report.properties.size(); ++propertyIndex) {
            report.properties[propertyIndex].nodeId = node.stableId;
        }
        return;
    }

    const SceneEntity entity = object.Entity();
    ScenePrefabTransformOverrideReporter::Append(scene, node, nodeIndex, object, report);
    ScenePrefabComponentOverrideReporter::Append(scene.Components(), entity, node.components, report, nodeIndex, object);
    for (std::size_t propertyIndex = firstProperty; propertyIndex < report.properties.size(); ++propertyIndex) {
        report.properties[propertyIndex].nodeId = node.stableId;
    }
}

} // namespace kb::scene
