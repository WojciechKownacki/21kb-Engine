#pragma once

#include "engine/ecs/ComponentId.hpp"
#include "engine/ecs/Query.hpp"

#include <cstddef>
#include <span>

struct ecs_query_t;
struct ecs_world_t;

namespace kb::ecs {

class QueryBatchDispatcher {
public:
    static void Execute(
        ecs_world_t* world,
        ecs_query_t* query,
        std::span<const std::size_t> componentSizes,
        bool filterChangedResults,
        std::size_t defaultExecutionGrainSize,
        QueryExecutionSettings settings,
        QueryRawBatchVisitor visitor,
        void* context);

    static void ExecuteMutable(
        ecs_world_t* world,
        ecs_query_t* query,
        std::span<const ComponentId> componentIds,
        std::span<const std::size_t> componentSizes,
        bool filterChangedResults,
        std::size_t defaultExecutionGrainSize,
        QueryExecutionSettings settings,
        QueryRawMutableBatchVisitor visitor,
        void* context);
};

} // namespace kb::ecs
