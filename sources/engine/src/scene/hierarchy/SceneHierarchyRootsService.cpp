#include "scene/hierarchy/SceneHierarchyRootsService.hpp"

#include "scene/SceneAccess.hpp"
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

std::vector<SceneObject> SceneHierarchyRootsService::RootObjects(Scene& scene) {
    std::vector<SceneObject> objects;
    for (const SceneEntity root : RootEntities(scene)) {
        objects.push_back(SceneAccess::MakeObject(scene, root));
    }

    return objects;
}

std::vector<SceneEntity> SceneHierarchyRootsService::RootEntities(const Scene& scene) {
    const SceneState& state = SceneAccess::State(scene);
    std::vector<SceneEntity> roots = SceneHierarchyOperations::Roots(state.world, state.components.TransformComponentId());
    SortByHierarchyOrder(state, roots);
    return roots;
}

} // namespace kb::scene
