#include "ecs/type/RelationTypeRegistry.hpp"

namespace kb::ecs {

RelationId RelationTypeRegistry::Register(ecs_world_t* world, std::type_index type, std::string_view name) {
    return cache_.Register(world, type, name);
}

RelationId RelationTypeRegistry::Find(std::type_index type) const noexcept {
    return cache_.Find(type);
}

void RelationTypeRegistry::Clear() noexcept {
    cache_.Clear();
}

} // namespace kb::ecs
