#include "engine/scene/SceneHierarchyAccess.hpp"

#include "scene/SceneHierarchyService.hpp"

namespace kb::scene {

SceneHierarchyQueries::SceneHierarchyQueries(const Scene& scene) noexcept
    : scene_(scene) {}

SceneEntity SceneHierarchyQueries::Parent(SceneEntity entity) const noexcept {
    return SceneHierarchyService::Parent(scene_, entity);
}

std::vector<SceneEntity> SceneHierarchyQueries::ChildEntities(SceneEntity entity) const {
    return SceneHierarchyService::ChildEntities(scene_, entity);
}

std::vector<SceneEntity> SceneHierarchyQueries::RootEntities() const {
    return SceneHierarchyService::RootEntities(scene_);
}

SceneHierarchyAccess::SceneHierarchyAccess(Scene& scene) noexcept
    : scene_(scene) {}

SceneObject SceneHierarchyAccess::Parent(SceneObject object) {
    return SceneHierarchyService::Parent(scene_, object);
}

SceneEntity SceneHierarchyAccess::Parent(SceneEntity entity) const noexcept {
    return SceneHierarchyService::Parent(scene_, entity);
}

std::vector<SceneObject> SceneHierarchyAccess::Children(SceneObject object) {
    return SceneHierarchyService::Children(scene_, object);
}

std::vector<SceneEntity> SceneHierarchyAccess::ChildEntities(SceneEntity entity) const {
    return SceneHierarchyService::ChildEntities(scene_, entity);
}

std::vector<SceneObject> SceneHierarchyAccess::RootObjects() {
    return SceneHierarchyService::RootObjects(scene_);
}

std::vector<SceneEntity> SceneHierarchyAccess::RootEntities() const {
    return SceneHierarchyService::RootEntities(scene_);
}

bool SceneHierarchyAccess::SetParent(SceneObject child, SceneObject parent) noexcept {
    return SceneHierarchyService::SetParent(scene_, child, parent);
}

bool SceneHierarchyAccess::SetParent(SceneEntity child, SceneEntity parent) noexcept {
    return SceneHierarchyService::SetParent(scene_, child, parent);
}

} // namespace kb::scene
