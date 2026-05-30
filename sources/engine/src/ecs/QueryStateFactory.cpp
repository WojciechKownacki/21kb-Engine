#include "ecs/QueryStateFactory.hpp"

#include "ecs/QueryState.hpp"

namespace kb::ecs {

QueryState* QueryStateFactory::Create(
    ecs_world_t* world,
    std::span<const ComponentId> componentIds,
    std::span<const std::size_t> componentSizes,
    const WorldConfig& config) {
    if (world == nullptr || componentIds.empty() || componentIds.size() != componentSizes.size()) {
        return nullptr;
    }

    return new QueryState{ world, componentIds, componentSizes, config.executionGrainSize };
}

} // namespace kb::ecs
