#include "ecs/ComponentStorage.hpp"

#include <flecs.h>

namespace kb::ecs {

bool ComponentStorage::Has(const ecs_world_t* world, Entity entity, ComponentId componentId) noexcept {
    return world != nullptr && entity.IsValid() && componentId != 0 && ecs_is_alive(world, entity.Id()) && ecs_has_id(world, entity.Id(), componentId);
}

const void* ComponentStorage::TryGet(const ecs_world_t* world, Entity entity, ComponentId componentId) noexcept {
    return Has(world, entity, componentId) ? ecs_get_id(world, entity.Id(), componentId) : nullptr;
}

void* ComponentStorage::TryGetMutable(ecs_world_t* world, Entity entity, ComponentId componentId) noexcept {
    return Has(world, entity, componentId) ? ecs_get_mut_id(world, entity.Id(), componentId) : nullptr;
}

} // namespace kb::ecs
