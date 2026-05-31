#include "scene/hierarchy/SceneHierarchyChildrenService.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"
#include "scene/hierarchy/SceneHierarchyOperations.hpp"

#include <algorithm>

namespace kb::scene {

namespace {

[[nodiscard]] std::uint64_t HierarchyOrder(const SceneState& state, SceneEntity entity) {
    const auto it = state.hierarchyOrder.find(entity.Id());
    return it != state.hierarchyOrder.end() ? it->second : entity.Id();
}

void SortByHierarchyOrder(const SceneState& state, std::vector<SceneEntity>& entities) {
    std::ranges::sort(entities, [&state](SceneEntity left, SceneEntity right) {
        const std::uint64_t leftOrder = HierarchyOrder(state, left);
        const std::uint64_t rightOrder = HierarchyOrder(state, right);
        if (leftOrder != rightOrder) {
            return leftOrder < rightOrder;
        }
        return left.Id() < right.Id();
    });
}

} // namespace

std::vector<SceneObject> SceneHierarchyChildrenService::Children(Scene& scene, SceneObject object) {
    if (!SceneEntityService::IsAlive(scene, object)) {
        return {};
    }

    std::vector<SceneObject> objects;
    for (const SceneEntity child : ChildEntities(scene, object.Entity())) {
        objects.push_back(SceneAccess::MakeObject(scene, child));
    }

    return objects;
}

std::vector<SceneEntity> SceneHierarchyChildrenService::ChildEntities(const Scene& scene, SceneEntity entity) {
    const SceneState& state = SceneAccess::State(scene);
    std::vector<SceneEntity> children = SceneHierarchyOperations::Children(state.world, entity);
    SortByHierarchyOrder(state, children);
    return children;
}

} // namespace kb::scene
