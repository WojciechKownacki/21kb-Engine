#include "ecs/relation/TagStorage.hpp"

#include <flecs.h>

namespace kb::ecs {

void TagStorage::Add(ecs_world_t* world, Entity entity, TagId tag) noexcept {
    if (world != nullptr && entity.IsValid() && tag != 0 && ecs_is_alive(world, entity.Id())) {
        ecs_add_id(world, entity.Id(), tag);
    }
}

bool TagStorage::Has(const ecs_world_t* world, Entity entity, TagId tag) noexcept {
    return world != nullptr && entity.IsValid() && tag != 0 && ecs_is_alive(world, entity.Id()) && ecs_has_id(world, entity.Id(), tag);
}

void TagStorage::Remove(ecs_world_t* world, Entity entity, TagId tag) noexcept {
    if (Has(world, entity, tag)) {
        ecs_remove_id(world, entity.Id(), tag);
    }
}

} // namespace kb::ecs
