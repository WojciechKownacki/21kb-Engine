#include "scene/prefab/ScenePrefabNodeComparator.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "scene/prefab/ScenePrefabComponentComparator.hpp"

namespace kb::scene {
namespace {

[[nodiscard]] bool Equal(Vec3 lhs, Vec3 rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

[[nodiscard]] bool Equal(Quat lhs, Quat rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
}

[[nodiscard]] bool EqualLocalTransform(const TransformComponent& lhs, const TransformComponent& rhs) noexcept {
    return Equal(lhs.localPosition, rhs.localPosition)
        && Equal(lhs.localRotation, rhs.localRotation)
        && Equal(lhs.localScale, rhs.localScale);
}

} // namespace

ScenePrefabOverrideFlag ScenePrefabNodeComparator::Compare(Scene& scene, SceneObject object, SceneObject expectedParent, const ScenePrefabNodeDesc& node) {
    if (!object.IsValid() || !scene.Entities().IsAlive(object)) {
        return ScenePrefabOverrideFlag::MissingObject;
    }

    ScenePrefabOverrideFlag flags = ScenePrefabOverrideFlag::None;
    const SceneEntity entity = object.Entity();
    if (scene.Entities().Name(object) != node.name) {
        flags |= ScenePrefabOverrideFlag::Name;
    }
    if (scene.Hierarchy().Parent(entity) != expectedParent.Entity()) {
        flags |= ScenePrefabOverrideFlag::Parent;
    }
    if (!EqualLocalTransform(scene.Transforms().Get(entity), node.transform)) {
        flags |= ScenePrefabOverrideFlag::Transform;
    }
    if (scene.Components().Visibility().Get(entity).visible != node.visibility.visible) {
        flags |= ScenePrefabOverrideFlag::Visibility;
    }

    return flags | ScenePrefabComponentComparator::Compare(scene.Components(), entity, node.components);
}

} // namespace kb::scene
