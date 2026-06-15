#include "engine/ecs/World.hpp"

#include "ecs/QueryStateFactory.hpp"
#include "ecs/query/QueryPlanCache.hpp"

#include <flecs.h>

#include <span>

namespace kb::ecs {

QueryState* World::CreateQueryState(
    const ComponentId* componentIds,
    const std::size_t* componentSizes,
    std::size_t componentCount,
    std::span<const ComponentId> requiredComponentIds,
    std::span<const ComponentId> optionalComponentIds,
    std::span<const ComponentId> excludedComponentIds,
    std::span<const ComponentId> changedComponentIds) const {
    if (world_ == nullptr || queryPlanCache_ == nullptr || componentIds == nullptr || componentSizes == nullptr || componentCount == 0) {
        return nullptr;
    }

    return QueryStateFactory::Create(
        world_,
        *queryPlanCache_,
        std::span<const ComponentId>{ componentIds, componentCount },
        std::span<const std::size_t>{ componentSizes, componentCount },
        requiredComponentIds,
        optionalComponentIds,
        excludedComponentIds,
        changedComponentIds,
        config_);
}

ecs_table_t* World::EntityArchetype(Entity entity) const noexcept {
    if (world_ == nullptr || !entity.IsValid() || !ecs_is_alive(world_, entity.Id())) {
        return nullptr;
    }
    return ecs_get_table(world_, entity.Id());
}

void World::InvalidateQueryPlansForArchetypeChange(ecs_table_t* previousArchetype, ecs_table_t* currentArchetype) noexcept {
    if (queryPlanCache_ != nullptr && previousArchetype != currentArchetype) {
        queryPlanCache_->InvalidateTouchedArchetypes(previousArchetype, currentArchetype);
    }
}

} // namespace kb::ecs
