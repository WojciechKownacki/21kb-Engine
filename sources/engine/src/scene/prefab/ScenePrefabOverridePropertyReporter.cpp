#include "scene/prefab/ScenePrefabOverridePropertyReporter.hpp"

#include "engine/scene/Scene.hpp"
#include "scene/prefab/ScenePrefabComponentOverrideReporter.hpp"
#include "scene/prefab/ScenePrefabObjectOverrideReporter.hpp"
#include "scene/prefab/ScenePrefabTransformOverrideReporter.hpp"

#include <utility>

namespace kb::scene {

void ScenePrefabOverridePropertyReporter::Add(ScenePrefabOverrideReport& report, std::uint32_t nodeIndex, SceneObject target, std::string propertyPath, std::string value, ScenePrefabOverrideFlag flag, SceneObject objectReference) {
    report.properties.push_back(ScenePrefabPropertyOverride{
        .nodeIndex = nodeIndex,
        .target = target,
        .propertyPath = std::move(propertyPath),
        .value = std::move(value),
        .objectReference = objectReference,
        .flag = flag,
    });
}

void ScenePrefabOverridePropertyReporter::AppendChangedProperties(Scene& scene, const ScenePrefabNodeDesc& node, SceneObject expectedParent, std::uint32_t nodeIndex, SceneObject object, ScenePrefabOverrideReport& report) {
    if (!ScenePrefabObjectOverrideReporter::Append(scene, node, expectedParent, nodeIndex, object, report)) {
        return;
    }

    const SceneEntity entity = object.Entity();
    ScenePrefabTransformOverrideReporter::Append(scene, node, nodeIndex, object, report);
    ScenePrefabComponentOverrideReporter::Append(scene.Components(), entity, node.components, report, nodeIndex, object);
}

} // namespace kb::scene
