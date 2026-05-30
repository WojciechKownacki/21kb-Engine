#pragma once

#include "engine/ecs/World.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class SceneHierarchyParenting {
public:
    SceneHierarchyParenting() = delete;

    [[nodiscard]] static bool SetParent(kb::ecs::World& world, SceneEntity child, SceneEntity parent) noexcept;

private:
    [[nodiscard]] static bool WouldCreateCycle(const kb::ecs::World& world, SceneEntity child, SceneEntity parent) noexcept;
};

} // namespace kb::scene
