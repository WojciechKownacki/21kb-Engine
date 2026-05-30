#pragma once

#include "engine/ecs/World.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>
#include <vector>

namespace kb::scene {

class SceneHierarchyRootCollector {
public:
    SceneHierarchyRootCollector() = delete;

    [[nodiscard]] static std::vector<SceneEntity> Roots(const kb::ecs::World& world, std::uint64_t transformComponentId);
};

} // namespace kb::scene
