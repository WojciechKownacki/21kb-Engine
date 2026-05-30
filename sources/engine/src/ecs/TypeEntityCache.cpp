#include "ecs/type/TypeEntityCache.hpp"

#include <flecs.h>

#include <string>

namespace kb::ecs {

Entity::IdType TypeEntityCache::Register(ecs_world_t* world, std::type_index type, std::string_view name) {
    if (const auto it = ids_.find(type); it != ids_.end()) {
        return it->second;
    }

    const Entity::IdType id = CreateTypeEntity(world, name);
    if (id != 0) {
        ids_.emplace(type, id);
    }
    return id;
}

Entity::IdType TypeEntityCache::Find(std::type_index type) const noexcept {
    const auto it = ids_.find(type);
    return it == ids_.end() ? 0 : it->second;
}

void TypeEntityCache::Clear() noexcept {
    ids_.clear();
}

Entity::IdType TypeEntityCache::CreateTypeEntity(ecs_world_t* world, std::string_view name) {
    if (world == nullptr) {
        return 0;
    }

    ecs_entity_desc_t desc{};
    std::string ownedName;
    if (!name.empty()) {
        ownedName = std::string{ name };
        desc.name = ownedName.c_str();
    }
    return static_cast<Entity::IdType>(ecs_entity_init(world, &desc));
}

} // namespace kb::ecs
