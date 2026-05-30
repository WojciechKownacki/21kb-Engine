#include "ecs/relation/RelationStorage.hpp"

#include <flecs.h>

namespace kb::ecs {

void RelationStorage::Add(ecs_world_t* world, Entity entity, RelationId relation, Entity target) noexcept {
    if (world != nullptr && entity.IsValid() && target.IsValid() && relation != 0 && ecs_is_alive(world, entity.Id()) && ecs_is_alive(world, target.Id())) {
        ecs_add_pair(world, entity.Id(), relation, target.Id());
    }
}

bool RelationStorage::Has(const ecs_world_t* world, Entity entity, RelationId relation, Entity target) noexcept {
    return world != nullptr && entity.IsValid() && target.IsValid() && relation != 0 && ecs_is_alive(world, entity.Id()) && ecs_has_pair(world, entity.Id(), relation, target.Id());
}

void RelationStorage::Remove(ecs_world_t* world, Entity entity, RelationId relation, Entity target) noexcept {
    if (Has(world, entity, relation, target)) {
        ecs_remove_pair(world, entity.Id(), relation, target.Id());
    }
}

Entity RelationStorage::Target(const ecs_world_t* world, Entity entity, RelationId relation, int index) noexcept {
    if (world == nullptr || !entity.IsValid() || relation == 0 || index < 0 || !ecs_is_alive(world, entity.Id())) {
        return {};
    }

    return Entity{ static_cast<Entity::IdType>(ecs_get_target(world, entity.Id(), relation, index)) };
}

} // namespace kb::ecs
