#include "scene/prefab/ScenePrefabTransformOverrideReporter.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "scene/prefab/ScenePrefabOverridePropertyReporter.hpp"
#include "scene/prefab/ScenePrefabOverrideValueFormatter.hpp"

namespace kb::scene {

void ScenePrefabTransformOverrideReporter::Append(Scene& scene, const ScenePrefabNodeDesc& node, std::uint32_t nodeIndex, SceneObject object, ScenePrefabOverrideReport& report) {
    const SceneEntity entity = object.Entity();
    const TransformComponent transform = scene.Transforms().Get(entity);
    if (!ScenePrefabOverrideValueFormatter::Equal(transform.localPosition, node.transform.localPosition)) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "transform.localPosition", ScenePrefabOverrideValueFormatter::ToString(transform.localPosition), ScenePrefabOverrideFlag::Transform);
    }
    if (!ScenePrefabOverrideValueFormatter::Equal(transform.localRotation, node.transform.localRotation)) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "transform.localRotation", ScenePrefabOverrideValueFormatter::ToString(transform.localRotation), ScenePrefabOverrideFlag::Transform);
    }
    if (!ScenePrefabOverrideValueFormatter::Equal(transform.localScale, node.transform.localScale)) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "transform.localScale", ScenePrefabOverrideValueFormatter::ToString(transform.localScale), ScenePrefabOverrideFlag::Transform);
    }

    const VisibilityComponent visibility = scene.Components().Visibility().Get(entity);
    if (visibility.visible != node.visibility.visible) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "visibility.visible", ScenePrefabOverrideValueFormatter::ToString(visibility.visible), ScenePrefabOverrideFlag::Visibility);
    }
}

} // namespace kb::scene
