#include "scene/hierarchy/SceneHierarchyOperations.hpp"

#include "scene/hierarchy/SceneHierarchyParenting.hpp"
#include "scene/hierarchy/SceneHierarchyReader.hpp"
#include "scene/hierarchy/SceneHierarchyRootCollector.hpp"

namespace kb::scene {

SceneEntity SceneHierarchyOperations::Parent(const kb::ecs::World& world, SceneEntity entity) noexcept {
    return SceneHierarchyReader::Parent(world, entity);
}

std::vector<SceneEntity> SceneHierarchyOperations::Children(const kb::ecs::World& world, SceneEntity entity) {
    return SceneHierarchyReader::Children(world, entity);
}

std::vector<SceneEntity> SceneHierarchyOperations::Roots(const kb::ecs::World& world, std::uint64_t transformComponentId) {
    return SceneHierarchyRootCollector::Roots(world, transformComponentId);
}

bool SceneHierarchyOperations::SetParent(kb::ecs::World& world, SceneEntity child, SceneEntity parent) noexcept {
    return SceneHierarchyParenting::SetParent(world, child, parent);
}

} // namespace kb::scene
