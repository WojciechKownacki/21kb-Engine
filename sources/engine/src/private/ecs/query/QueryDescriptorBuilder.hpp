#pragma once

#include "engine/ecs/ComponentId.hpp"
#include "ecs/query/FlecsQueryHandle.hpp"

#include <cstddef>
#include <span>

struct ecs_world_t;

namespace kb::ecs {

class QueryDescriptorBuilder {
public:
    [[nodiscard]] static FlecsQueryHandle Build(
        ecs_world_t* world,
        std::span<const ComponentId> componentIds,
        std::span<const std::size_t> componentSizes,
        std::span<const ComponentId> requiredComponentIds,
        std::span<const ComponentId> optionalComponentIds,
        std::span<const ComponentId> excludedComponentIds,
        std::span<const ComponentId> changedComponentIds);

    [[nodiscard]] static FlecsQueryHandle Build(
        ecs_world_t* world,
        std::span<const ComponentId> componentIds,
        std::span<const std::size_t> componentSizes);
};

} // namespace kb::ecs
