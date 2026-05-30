#include "engine/ecs/World.hpp"

#include <flecs.h>

#include <string>

namespace kb::ecs {

Entity World::CreateEntity() {
    return Entity{ ecs_new(world_) };
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
    if (world_ != nullptr && entity.IsValid() && ecs_is_valid(world_, entity.Id())) {
        ecs_delete(world_, entity.Id());
    }
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
