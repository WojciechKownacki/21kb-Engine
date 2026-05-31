#include "scene/prefab/ScenePrefabPropertyReverter.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "scene/prefab/ScenePrefabInstanceTopology.hpp"
#include "scene/prefab/ScenePrefabPropertyPath.hpp"

namespace kb::scene {
namespace {

void RevertComponentProperty(Scene& scene, SceneObject object, const ScenePrefabNodeDesc& node, std::string_view propertyPath) {
    SceneComponents components = scene.Components();
    const SceneEntity entity = object.Entity();
    if (ScenePrefabPropertyPath::StartsWith(propertyPath, "camera")) {
        if (node.components.camera.has_value()) {
            components.Cameras().Set(entity, *node.components.camera);
        } else {
            components.Cameras().Remove(entity);
        }
    } else if (ScenePrefabPropertyPath::StartsWith(propertyPath, "meshRenderer")) {
        if (node.components.meshRenderer.has_value()) {
            components.MeshRenderers().Set(entity, *node.components.meshRenderer);
        } else {
            components.MeshRenderers().Remove(entity);
        }
    } else if (ScenePrefabPropertyPath::StartsWith(propertyPath, "light")) {
        if (node.components.light.has_value()) {
            components.Lights().Set(entity, *node.components.light);
        } else {
            components.Lights().Remove(entity);
        }
    }
}

} // namespace

bool ScenePrefabPropertyReverter::Revert(Scene& scene, const ScenePrefabInstanceRecord& instance, SceneObject object, const ScenePrefabNodeDesc& node, std::string_view propertyPath) {
    if (propertyPath == "name") {
        scene.Entities().SetName(object, node.name);
        return true;
    }
    if (propertyPath == "parent") {
        return scene.Hierarchy().SetParent(object, ScenePrefabInstanceTopology::ExpectedParent(node, instance));
    }
    if (ScenePrefabPropertyPath::IsTransform(propertyPath)) {
        TransformComponent transform = scene.Transforms().Get(object);
        if (propertyPath == "transform.localPosition") {
            transform.localPosition = node.transform.localPosition;
        } else if (propertyPath == "transform.localRotation") {
            transform.localRotation = node.transform.localRotation;
        } else if (propertyPath == "transform.localScale") {
            transform.localScale = node.transform.localScale;
        } else {
            return false;
        }
        scene.Transforms().Set(object, transform);
        return true;
    }
    if (propertyPath == "visibility.visible") {
        scene.Components().Visibility().Set(object.Entity(), node.visibility);
        return true;
    }
    if (ScenePrefabPropertyPath::IsComponent(propertyPath)) {
        RevertComponentProperty(scene, object, node, propertyPath);
        return true;
    }
    return false;
}

} // namespace kb::scene
