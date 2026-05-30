#pragma once

#include "engine/ecs/World.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>
#include <vector>

namespace kb::scene {

class SceneHierarchyOperations {
public:
    SceneHierarchyOperations() = delete;

    [[nodiscard]] static SceneEntity Parent(const kb::ecs::World& world, SceneEntity entity) noexcept;
    [[nodiscard]] static std::vector<SceneEntity> Children(const kb::ecs::World& world, SceneEntity entity);
    [[nodiscard]] static std::vector<SceneEntity> Roots(const kb::ecs::World& world, std::uint64_t transformComponentId);
    [[nodiscard]] static bool SetParent(kb::ecs::World& world, SceneEntity child, SceneEntity parent) noexcept;

private:
};

} // namespace kb::scene
