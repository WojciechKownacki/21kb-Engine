#include "ecs/component/ComponentStorageQuery.hpp"

#include "ecs/FlecsEntityIds.hpp"

#include <flecs.h>

namespace kb::ecs {

bool ComponentStorageQuery::Has(const ecs_world_t* world, Entity entity, ComponentId componentId) noexcept {
    return world != nullptr && entity.IsValid() && componentId != 0 && ecs_is_alive(world, FlecsEntityId(entity)) &&
           ecs_has_id(world, FlecsEntityId(entity), componentId);
}

const void* ComponentStorageQuery::TryGet(const ecs_world_t* world, Entity entity, ComponentId componentId) noexcept {
    return Has(world, entity, componentId) ? ecs_get_id(world, FlecsEntityId(entity), componentId) : nullptr;
}

void* ComponentStorageQuery::TryGetMutable(ecs_world_t* world, Entity entity, ComponentId componentId) noexcept {
    return Has(world, entity, componentId) ? ecs_get_mut_id(world, FlecsEntityId(entity), componentId) : nullptr;
}

} // namespace kb::ecs
