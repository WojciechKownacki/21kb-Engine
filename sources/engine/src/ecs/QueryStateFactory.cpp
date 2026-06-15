#include "ecs/QueryStateFactory.hpp"

#include "ecs/QueryState.hpp"
#include "ecs/query/QueryPlanCache.hpp"

#include <memory>
#include <utility>

namespace kb::ecs {

QueryState* QueryStateFactory::Create(
    ecs_world_t* world,
    QueryPlanCache& queryPlanCache,
    std::span<const ComponentId> componentIds,
    std::span<const std::size_t> componentSizes,
    std::span<const ComponentId> requiredComponentIds,
    std::span<const ComponentId> optionalComponentIds,
    std::span<const ComponentId> excludedComponentIds,
    std::span<const ComponentId> changedComponentIds,
    const WorldConfig& config) {
    if (world == nullptr || componentIds.empty() || componentIds.size() != componentSizes.size()) {
        return nullptr;
    }

    std::shared_ptr<QueryPlan> plan;
    if (changedComponentIds.empty()) {
        plan = queryPlanCache.GetOrCreate(world, componentIds, componentSizes, requiredComponentIds, optionalComponentIds, excludedComponentIds);
    } else {
        plan = std::make_shared<QueryPlan>(
            world,
            componentIds,
            componentSizes,
            requiredComponentIds,
            optionalComponentIds,
            excludedComponentIds,
            changedComponentIds);
    }
    if (!plan) {
        return nullptr;
    }

    return new QueryState{ world, std::move(plan), config.executionGrainSize };
}

} // namespace kb::ecs
