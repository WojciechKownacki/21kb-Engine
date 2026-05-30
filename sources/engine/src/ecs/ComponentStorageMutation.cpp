#include "ecs/component/ComponentStorageMutation.hpp"

#include "ecs/component/ComponentStorageQuery.hpp"

#include <flecs.h>

namespace kb::ecs {

void ComponentStorageMutation::Set(ecs_world_t* world, Entity entity, ComponentId componentId, std::size_t size, const void* component) {
    if (world != nullptr && entity.IsValid() && componentId != 0 && size != 0 && component != nullptr && ecs_is_alive(world, entity.Id())) {
        ecs_set_id(world, entity.Id(), componentId, size, component);
    }
}

void ComponentStorageMutation::Remove(ecs_world_t* world, Entity entity, ComponentId componentId) noexcept {
    if (ComponentStorageQuery::Has(world, entity, componentId)) {
        ecs_remove_id(world, entity.Id(), componentId);
    }
}

void ComponentStorageMutation::MarkModified(ecs_world_t* world, Entity entity, ComponentId componentId) noexcept {
    if (ComponentStorageQuery::Has(world, entity, componentId)) {
        ecs_modified_id(world, entity.Id(), componentId);
    }
}

} // namespace kb::ecs
