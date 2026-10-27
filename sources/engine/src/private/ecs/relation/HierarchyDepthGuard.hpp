#pragma once

#include "engine/ecs/Entity.hpp"

#include <cstddef>
#include <span>

struct ecs_world_t;

namespace kb::ecs {

enum class NewEntityParentBatchDepth { Fits, RequiresGeneralAssignment, Rejected };

[[nodiscard]] NewEntityParentBatchDepth ValidateNewEntityParentBatchDepth(
    ecs_world_t* world, std::span<const Entity> children, std::span<const Entity> parents) noexcept;
[[nodiscard]] std::size_t HierarchyDepthLimit() noexcept;
[[nodiscard]] bool FitsHierarchyDepth(ecs_world_t* world, Entity child, Entity parent) noexcept;

} // namespace kb::ecs
