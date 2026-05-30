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
        std::span<const std::size_t> componentSizes);
};

} // namespace kb::ecs
