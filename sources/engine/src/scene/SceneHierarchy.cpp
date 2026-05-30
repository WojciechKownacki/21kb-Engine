#include "engine/scene/Scene.hpp"

#include "scene/components/SceneComponentRegistry.hpp"
#include "scene/hierarchy/SceneHierarchyOperations.hpp"

namespace kb::scene {

SceneObject Scene::Parent(SceneObject object) const {
    if (!IsAlive(object)) {
        return {};
    }

    const SceneEntity parent = Parent(object.Entity());
    return parent.IsValid() ? const_cast<Scene*>(this)->MakeObject(parent) : SceneObject{};
}

SceneEntity Scene::Parent(SceneEntity entity) const noexcept {
    return SceneHierarchyOperations::Parent(world_, entity);
}

std::vector<SceneObject> Scene::Children(SceneObject object) const {
    if (!IsAlive(object)) {
        return {};
    }

    std::vector<SceneObject> objects;
    for (const SceneEntity child : ChildEntities(object.Entity())) {
        objects.push_back(const_cast<Scene*>(this)->MakeObject(child));
    }

    return objects;
}

std::vector<SceneEntity> Scene::ChildEntities(SceneEntity entity) const {
    return SceneHierarchyOperations::Children(world_, entity);
}

std::vector<SceneObject> Scene::RootObjects() const {
    std::vector<SceneObject> objects;
    for (const SceneEntity root : RootEntities()) {
        objects.push_back(const_cast<Scene*>(this)->MakeObject(root));
    }

    return objects;
}

std::vector<SceneEntity> Scene::RootEntities() const {
    return SceneHierarchyOperations::Roots(world_, components_->TransformComponentId());
}

bool Scene::SetParent(SceneObject child, SceneObject parent) noexcept {
    if (!BelongsToThisScene(child)) {
        return false;
    }
    if (parent.Entity().IsValid() && !BelongsToThisScene(parent)) {
        return false;
    }

    return SetParent(child.Entity(), parent.Entity());
}

bool Scene::SetParent(SceneEntity child, SceneEntity parent) noexcept {
    return SceneHierarchyOperations::SetParent(world_, child, parent);
}

} // namespace kb::scene
