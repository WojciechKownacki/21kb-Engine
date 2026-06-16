#include "ecs/relation/RelationStorage.hpp"

#include "ecs/FlecsEntityIds.hpp"

#include <flecs.h>

namespace kb::ecs {
namespace {

[[nodiscard]] ecs_entity_t PairTarget(Entity target) noexcept {
    return FlecsEntityId(target);
}

} // namespace

void RelationStorage::Add(ecs_world_t* world, Entity entity, RelationId relation, Entity target) noexcept {
    if (world != nullptr && entity.IsValid() && target.IsValid() && relation != 0 && ecs_is_alive(world, FlecsEntityId(entity)) &&
        ecs_is_alive(world, FlecsEntityId(target))) {
        ecs_add_pair(world, FlecsEntityId(entity), relation, PairTarget(target));
    }
}

bool RelationStorage::Has(const ecs_world_t* world, Entity entity, RelationId relation, Entity target) noexcept {
    return world != nullptr && entity.IsValid() && target.IsValid() && relation != 0 && ecs_is_alive(world, FlecsEntityId(entity)) &&
           ecs_has_pair(world, FlecsEntityId(entity), relation, PairTarget(target));
}

void RelationStorage::Remove(ecs_world_t* world, Entity entity, RelationId relation, Entity target) noexcept {
    if (world != nullptr && entity.IsValid() && target.IsValid() && relation != 0 && ecs_is_alive(world, FlecsEntityId(entity))) {
        ecs_remove_pair(world, FlecsEntityId(entity), relation, PairTarget(target));
    }
}

Entity RelationStorage::Target(const ecs_world_t* world, Entity entity, RelationId relation, int index) noexcept {
    if (world == nullptr || !entity.IsValid() || relation == 0 || index < 0 || !ecs_is_alive(world, FlecsEntityId(entity))) {
        return {};
    }

    return Entity{ static_cast<Entity::IdType>(ecs_get_target(world, FlecsEntityId(entity), relation, index)) };
}

} // namespace kb::ecs
