#pragma once

#include "engine/ecs/ComponentId.hpp"
#include "engine/ecs/WorldConfig.hpp"

#include <cstddef>
#include <span>

struct ecs_world_t;

namespace kb::ecs {

class QueryState;

class QueryStateFactory {
public:
    [[nodiscard]] static QueryState* Create(
        ecs_world_t* world,
        std::span<const ComponentId> componentIds,
        std::span<const std::size_t> componentSizes,
        const WorldConfig& config);
};

} // namespace kb::ecs
