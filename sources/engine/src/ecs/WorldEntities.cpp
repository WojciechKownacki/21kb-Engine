#include "engine/ecs/World.hpp"

#include "ecs/world/WorldEntityCatalog.hpp"
#include "ecs/world/WorldRegistrySet.hpp"

#include <flecs.h>

#include <string>

namespace kb::ecs {

Entity World::CreateEntity() {
    const Entity entity{ ecs_new(world_) };
    if (registries_ != nullptr) {
        registries_->Entities().Add(entity);
    }
    return entity;
}

Entity World::CreateEntity(std::string_view name) {
    Entity entity = CreateEntity();
    if (!name.empty()) {
        const std::string ownedName{ name };
        ecs_set_name(world_, entity.Id(), ownedName.c_str());
    }
    return entity;
}

void World::DestroyEntity(Entity entity) noexcept {
    ecs_table_t* previousArchetype = EntityArchetype(entity);
    if (world_ != nullptr && entity.IsValid() && ecs_is_valid(world_, entity.Id())) {
        ecs_delete(world_, entity.Id());
    }
    InvalidateQueryPlansForArchetypeChange(previousArchetype, EntityArchetype(entity));
    if (registries_ != nullptr) {
        registries_->Entities().Remove(entity);
    }
}

void World::SetName(Entity entity, std::string_view name) {
    if (!IsAlive(entity)) {
        return;
    }

    const std::string ownedName{ name };
    ecs_set_name(world_, entity.Id(), ownedName.empty() ? nullptr : ownedName.c_str());
}

bool World::IsAlive(Entity entity) const noexcept {
    return world_ != nullptr && entity.IsValid() && ecs_is_valid(world_, entity.Id()) && ecs_is_alive(world_, entity.Id());
}

std::string World::Name(Entity entity) const {
    if (!IsAlive(entity)) {
        return {};
    }

    if (const char* name = ecs_get_name(world_, entity.Id()); name != nullptr) {
        return std::string{ name };
    }

    return {};
}

} // namespace kb::ecs
