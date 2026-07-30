#include "scene/prefab/ScenePrefabPropertyApplier.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "scene/prefab/ScenePrefabComponentSnapshot.hpp"
#include "scene/prefab/ScenePrefabPropertyPath.hpp"

namespace kb::scene {

bool ScenePrefabPropertyApplier::Apply(Scene& scene, ScenePrefabNodeDesc& node, SceneObject object, std::string_view propertyPath) {
    if (propertyPath == "name") {
        node.name = scene.Entities().Name(object);
        return true;
    }
    if (propertyPath == "parent") {
        return false;
    }
    if (ScenePrefabPropertyPath::IsTransform(propertyPath)) {
        const TransformComponent transform = scene.Transforms().Get(object);
        if (propertyPath == "transform.localPosition") {
            node.transform.localPosition = transform.localPosition;
        } else if (propertyPath == "transform.localRotation") {
            node.transform.localRotation = transform.localRotation;
        } else if (propertyPath == "transform.localScale") {
            node.transform.localScale = transform.localScale;
        } else {
            return false;
        }
        return true;
    }
    if (ScenePrefabPropertyPath::StartsWith(propertyPath, "visibility.")) {
        node.visibility = scene.Components().Visibility().Get(object.Entity());
        return true;
    }
    if (ScenePrefabPropertyPath::IsComponent(propertyPath)) {
        node.components = ScenePrefabComponentSnapshot::Capture(scene, object);
        return true;
    }
    return false;
}

} // namespace kb::scene
