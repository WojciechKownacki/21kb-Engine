#pragma once

#include "engine/ecs/World.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>
#include <vector>

namespace kb::scene {

class SceneTransformRootCollector {
public:
    [[nodiscard]] std::vector<SceneEntity> Collect(const kb::ecs::World& world, std::uint64_t transformComponentId) const;
};

} // namespace kb::scene
