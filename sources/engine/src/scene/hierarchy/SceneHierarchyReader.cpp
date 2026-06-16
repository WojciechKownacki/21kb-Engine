#include "scene/hierarchy/SceneHierarchyReader.hpp"

#include <algorithm>

namespace kb::scene {

SceneEntity SceneHierarchyReader::Parent(const kb::ecs::World& world, SceneEntity entity) noexcept {
    if (!world.IsAlive(entity)) {
        return {};
    }

    return world.Parent(entity);
}

std::vector<SceneEntity> SceneHierarchyReader::Children(const kb::ecs::World& world, SceneEntity entity) {
    if (!world.IsAlive(entity)) {
        return {};
    }

    std::vector<SceneEntity> children = world.Children(entity);

    std::ranges::sort(children, [](SceneEntity left, SceneEntity right) {
        return left.Id() < right.Id();
    });
    return children;
}

} // namespace kb::scene
