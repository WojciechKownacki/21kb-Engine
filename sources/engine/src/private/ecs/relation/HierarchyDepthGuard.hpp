#pragma once

#include "engine/ecs/Entity.hpp"

#include <cstddef>

struct ecs_world_t;

namespace kb::ecs {

[[nodiscard]] std::size_t HierarchyDepthLimit() noexcept;
[[nodiscard]] bool FitsHierarchyDepth(ecs_world_t* world, Entity child, Entity parent) noexcept;

} // namespace kb::ecs
