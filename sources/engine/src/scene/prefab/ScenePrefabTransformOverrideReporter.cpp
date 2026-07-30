#include "scene/prefab/ScenePrefabTransformOverrideReporter.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "scene/prefab/ScenePrefabOverridePropertyReporter.hpp"
#include "scene/prefab/ScenePrefabOverrideValueFormatter.hpp"

#include <cstdint>
#include <string>

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
    if (visibility.mode != node.visibility.mode) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "visibility.visible", ScenePrefabOverrideValueFormatter::ToString(visibility.mode != VisibilityMode::Hidden), ScenePrefabOverrideFlag::Visibility);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "visibility.mode", std::to_string(static_cast<std::uint32_t>(visibility.mode)), ScenePrefabOverrideFlag::Visibility);
    }
    if (visibility.mask != node.visibility.mask) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "visibility.mask", std::to_string(visibility.mask), ScenePrefabOverrideFlag::Visibility);
    }
}

} // namespace kb::scene
