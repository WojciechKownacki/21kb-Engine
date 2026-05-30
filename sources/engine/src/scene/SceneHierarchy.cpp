#include "scene/SceneHierarchyService.hpp"
#include "scene/hierarchy/SceneHierarchyChildrenService.hpp"
#include "scene/hierarchy/SceneHierarchyParentAssignmentService.hpp"
#include "scene/hierarchy/SceneHierarchyParentService.hpp"
#include "scene/hierarchy/SceneHierarchyRootsService.hpp"

namespace kb::scene {

SceneObject SceneHierarchyService::Parent(Scene& scene, SceneObject object) {
    return SceneHierarchyParentService::Parent(scene, object);
}

SceneEntity SceneHierarchyService::Parent(const Scene& scene, SceneEntity entity) noexcept {
    return SceneHierarchyParentService::Parent(scene, entity);
}

std::vector<SceneObject> SceneHierarchyService::Children(Scene& scene, SceneObject object) {
    return SceneHierarchyChildrenService::Children(scene, object);
}

std::vector<SceneEntity> SceneHierarchyService::ChildEntities(const Scene& scene, SceneEntity entity) {
    return SceneHierarchyChildrenService::ChildEntities(scene, entity);
}

std::vector<SceneObject> SceneHierarchyService::RootObjects(Scene& scene) {
    return SceneHierarchyRootsService::RootObjects(scene);
}

std::vector<SceneEntity> SceneHierarchyService::RootEntities(const Scene& scene) {
    return SceneHierarchyRootsService::RootEntities(scene);
}

bool SceneHierarchyService::SetParent(Scene& scene, SceneObject child, SceneObject parent) noexcept {
    return SceneHierarchyParentAssignmentService::SetParent(scene, child, parent);
}

bool SceneHierarchyService::SetParent(Scene& scene, SceneEntity child, SceneEntity parent) noexcept {
    return SceneHierarchyParentAssignmentService::SetParent(scene, child, parent);
}

} // namespace kb::scene
