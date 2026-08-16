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
    } else if (ScenePrefabPropertyPath::StartsWith(propertyPath, "input")) {
        if (node.components.input.has_value()) {
            components.Inputs().Set(entity, *node.components.input);
        } else {
            components.Inputs().Remove(entity);
        }
    } else if (ScenePrefabPropertyPath::StartsWith(propertyPath, "rigidbody")) {
        if (node.components.rigidbody.has_value()) {
            components.Rigidbodies().Set(entity, *node.components.rigidbody);
        } else {
            components.Rigidbodies().Remove(entity);
        }
    } else if (ScenePrefabPropertyPath::StartsWith(propertyPath, "collider")) {
        if (node.components.collider.has_value()) {
            components.Colliders().Set(entity, *node.components.collider);
        } else {
            components.Colliders().Remove(entity);
        }
    } else if (ScenePrefabPropertyPath::StartsWith(propertyPath, "tags")) {
        if (node.components.tags.has_value()) {
            components.Tags().Set(entity, *node.components.tags);
        } else {
            components.Tags().Remove(entity);
        }
    } else if (ScenePrefabPropertyPath::StartsWith(propertyPath, "behaviour")) {
        if (node.components.behaviour.has_value()) {
            components.Behaviours().Set(entity, *node.components.behaviour);
        } else {
            components.Behaviours().Remove(entity);
        }
    } else if (ScenePrefabPropertyPath::StartsWith(propertyPath, "audioSource")) {
        if (node.components.audioSource.has_value()) {
            components.AudioSources().Set(entity, *node.components.audioSource);
        } else {
            components.AudioSources().Remove(entity);
        }
    } else if (ScenePrefabPropertyPath::StartsWith(propertyPath, "audioListener")) {
        if (node.components.audioListener.has_value()) {
            components.AudioListeners().Set(entity, *node.components.audioListener);
        } else {
            components.AudioListeners().Remove(entity);
        }
    } else if (ScenePrefabPropertyPath::StartsWith(propertyPath, "animator")) {
        if (node.components.animator.has_value()) {
            components.Animators().Set(entity, *node.components.animator);
        } else {
            components.Animators().Remove(entity);
        }
    } else if (ScenePrefabPropertyPath::StartsWith(propertyPath, "skeletonBinding")) {
        if (node.components.skeletonBinding.has_value()) {
            static_cast<void>(components.SkeletonBindings().Set(entity, *node.components.skeletonBinding));
        } else {
            components.SkeletonBindings().Remove(entity);
        }
    } else if (ScenePrefabPropertyPath::StartsWith(propertyPath, "motionSkeletonRule")) {
        if (node.components.motionSkeletonRule.has_value()) {
            static_cast<void>(components.MotionSkeletonRules().Set(entity, *node.components.motionSkeletonRule));
        } else {
            components.MotionSkeletonRules().Remove(entity);
        }
    } else if (ScenePrefabPropertyPath::StartsWith(propertyPath, "deformedGeometry")) {
        if (node.components.deformedGeometry.has_value()) {
            static_cast<void>(components.DeformedGeometries().Set(entity, *node.components.deformedGeometry));
        } else {
            components.DeformedGeometries().Remove(entity);
        }
    } else if (ScenePrefabPropertyPath::StartsWith(propertyPath, "particleEffect")) {
        if (node.components.particleEffect.has_value()) {
            components.ParticleEffects().Set(entity, *node.components.particleEffect);
        } else {
            components.ParticleEffects().Remove(entity);
        }
    } else if (ScenePrefabPropertyPath::StartsWith(propertyPath, "uiDocument")) {
        if (node.components.uiDocument.has_value()) {
            components.UIDocuments().Set(entity, *node.components.uiDocument);
        } else {
            components.UIDocuments().Remove(entity);
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
    if (ScenePrefabPropertyPath::StartsWith(propertyPath, "visibility.")) {
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
