#include "ecs/ComponentStorage.hpp"

#include <flecs.h>

namespace kb::ecs {

void ComponentStorage::Set(ecs_world_t* world, Entity entity, ComponentId componentId, std::size_t size, const void* component) {
    if (world != nullptr && entity.IsValid() && componentId != 0 && size != 0 && component != nullptr && ecs_is_alive(world, entity.Id())) {
        ecs_set_id(world, entity.Id(), componentId, size, component);
    }
}

void ComponentStorage::Remove(ecs_world_t* world, Entity entity, ComponentId componentId) noexcept {
    if (Has(world, entity, componentId)) {
        ecs_remove_id(world, entity.Id(), componentId);
    }
}

void ComponentStorage::MarkModified(ecs_world_t* world, Entity entity, ComponentId componentId) noexcept {
    if (Has(world, entity, componentId)) {
        ecs_modified_id(world, entity.Id(), componentId);
    }
}

} // namespace kb::ecs
