#pragma once

#include "engine/ecs/World.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <vector>

namespace kb::scene {

class SceneHierarchyReader {
public:
    SceneHierarchyReader() = delete;

    [[nodiscard]] static SceneEntity Parent(const kb::ecs::World& world, SceneEntity entity) noexcept;
    [[nodiscard]] static std::vector<SceneEntity> Children(const kb::ecs::World& world, SceneEntity entity);
};

} // namespace kb::scene
